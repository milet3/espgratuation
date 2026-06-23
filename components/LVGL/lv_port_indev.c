/* ============================================================
 * lv_port_indev.c -- LVGL input device adapt (XPT2046 touch)
 * ============================================================ */
#include "lvgl.h"
#include "touch_driver.h"
#include "esp_log.h"

#define TAG "INDEV"

/* Touch orientation: uncomment to flip */
#define TOUCH_FLIP_X   1
#define TOUCH_FLIP_Y   1
/* #define TOUCH_SWAP_XY  1 */

/* ====== Sensitivity Tuning ====== */
#define TOUCH_X_MIN  50    /* Lower = catch lighter edge touches */
#define TOUCH_X_MAX  4000  /* Higher = catch edge-to-edge */
#define TOUCH_Y_MIN  50
#define TOUCH_Y_MAX  4000
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

/* Debounce: require N consecutive same-state readings */
#define DEBOUNCE_COUNT  1

/* Linear map */
static uint16_t calibrate(uint16_t raw, uint16_t raw_min,
                          uint16_t raw_max, uint16_t screen_max)
{
    if (raw < raw_min) raw = raw_min;
    if (raw > raw_max) raw = raw_max;
    return (uint16_t)((uint32_t)(raw - raw_min) * screen_max / (raw_max - raw_min));
}

/* Touch read callback */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static int16_t last_x = 0, last_y = 0;
    static int dbg_count = 0;
    static int press_count = 0, release_count = 0;
    static bool stable_state = false;
    uint16_t x, y;
    bool raw_pressed;

    if (touch_get_xy(&x, &y, &raw_pressed))
    {
        /* Position always updates (no filtering on coordinates) */
        uint16_t cx = calibrate(x, TOUCH_X_MIN, TOUCH_X_MAX, SCREEN_WIDTH);
        uint16_t cy = calibrate(y, TOUCH_Y_MIN, TOUCH_Y_MAX, SCREEN_HEIGHT);
#if TOUCH_FLIP_X
        cx = SCREEN_WIDTH - 1 - cx;
#endif
#if TOUCH_FLIP_Y
        cy = SCREEN_HEIGHT - 1 - cy;
#endif
#if TOUCH_SWAP_XY
        data->point.x = cy;
        data->point.y = cx;
#else
        data->point.x = cx;
        data->point.y = cy;
#endif

        /* Debounce press/release state */
        if (raw_pressed) {
            press_count++;
            release_count = 0;
            if (press_count >= DEBOUNCE_COUNT) {
                stable_state = true;
            }
        } else {
            release_count++;
            press_count = 0;
            if (release_count >= DEBOUNCE_COUNT) {
                stable_state = false;
            }
        }

        data->state = stable_state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

        if (stable_state) {
            last_x = data->point.x;
            last_y = data->point.y;
        } else {
            data->point.x = last_x;
            data->point.y = last_y;
        }

        if (++dbg_count % 100 == 0) {
            ESP_LOGI(TAG, "raw=(%u,%u) scr=(%d,%d) pressed=%d stable=%d",
                     x, y, data->point.x, data->point.y,
                     raw_pressed, stable_state);
        }
    }
    else
    {
        /* SPI error - release and keep last position */
        release_count++;
        press_count = 0;
        if (release_count >= DEBOUNCE_COUNT) {
            stable_state = false;
        }
        data->state = stable_state ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
    }
}

void lv_port_indev_init(void)
{
    touch_driver_init();

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    ESP_LOGI(TAG, "Touch input device registered (pointer)");
    ESP_LOGI(TAG, "Calibration: X[%d-%d] Y[%d-%d] -> %dx%d, debounce=%d",
             TOUCH_X_MIN, TOUCH_X_MAX, TOUCH_Y_MIN, TOUCH_Y_MAX,
             SCREEN_WIDTH, SCREEN_HEIGHT, DEBOUNCE_COUNT);
}
