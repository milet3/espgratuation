/* ============================================================
 * lv_port_disp.c -- LVGL display adapter (ILI9341 + ESP32 SPI)
 * v5: 60 FPS optimized — RGB565_SWAPPED, 60MHz SPI, single DMA
 * ============================================================ */
#include "lvgl.h"
#include "app_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"

#define TAG "LCD"
#define HOR_RES 320
#define VER_RES 240

/* SPI speed: ILI9341 can handle 60 MHz with short wiring */
#define LCD_SPI_SPEED_HZ (60 * 1000 * 1000)

static spi_device_handle_t lcd_spi;

/* Full-screen framebuffer in PSRAM.
 * RGB565_SWAPPED mode: LVGL writes pixels in ILI9341 big-endian [hi,lo]
 * — no byte-swap needed in flush! */
static uint8_t *fb = NULL;

/* -- SPI helpers -- */
static inline void lcd_write_cmd(uint8_t cmd)
{
    gpio_set_level(TFT_DC, 0);
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
    spi_device_polling_transmit(lcd_spi, &t);
}
static inline void lcd_write_data8(uint8_t data)
{
    gpio_set_level(TFT_DC, 1);
    spi_transaction_t t = {.length = 8, .tx_buffer = &data};
    spi_device_polling_transmit(lcd_spi, &t);
}
static inline void lcd_write_data16(uint16_t data)
{
    gpio_set_level(TFT_DC, 1);
    spi_transaction_t t = {.length = 16, .tx_buffer = &data};
    spi_device_polling_transmit(lcd_spi, &t);
}

/* Set window — coords already big-endian for ILI9341 */
static void lcd_set_window(uint16_t x1, uint16_t y1,
                           uint16_t x2, uint16_t y2)
{
    lcd_write_cmd(0x2A);
    lcd_write_data16((x1 >> 8) | (x1 << 8));
    lcd_write_data16((x2 >> 8) | (x2 << 8));
    lcd_write_cmd(0x2B);
    lcd_write_data16((y1 >> 8) | (y1 << 8));
    lcd_write_data16((y2 >> 8) | (y2 << 8));
    lcd_write_cmd(0x2C);
}

/* Flush dirty area — single DMA burst, no byte-swap needed! */
static void lcd_flush_dirty(const lv_area_t *area)
{
    uint16_t x1 = area->x1, y1 = area->y1;
    uint16_t x2 = area->x2, y2 = area->y2;
    uint32_t w = x2 - x1 + 1;
    uint32_t h = y2 - y1 + 1;

    lcd_set_window(x1, y1, x2, y2);
    gpio_set_level(TFT_DC, 1);

    for (uint32_t r = 0; r < h; r++)
    {
        const uint8_t *src = fb + ((y1 + r) * HOR_RES + x1) * 2;
        spi_transaction_t t = {
            .length = w * 16,
            .tx_buffer = src, /* no byte-swap — LVGL already big-endian */
        };
        spi_device_transmit(lcd_spi, &t);
    }
}

/* LVGL flush callback */
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map)
{
    (void)px_map;
    lcd_flush_dirty(area);
    lv_display_flush_ready(disp);
}

/* ILI9341 init sequence */
static void lcd_init_hw(void)
{
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    lcd_write_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));
    lcd_write_cmd(0xCB);
    lcd_write_data8(0x39);
    lcd_write_data8(0x2C);
    lcd_write_data8(0x00);
    lcd_write_data8(0x34);
    lcd_write_data8(0x02);
    lcd_write_cmd(0xCF);
    lcd_write_data8(0x00);
    lcd_write_data8(0xC1);
    lcd_write_data8(0x30);
    lcd_write_cmd(0xE8);
    lcd_write_data8(0x85);
    lcd_write_data8(0x00);
    lcd_write_data8(0x78);
    lcd_write_cmd(0xEA);
    lcd_write_data8(0x00);
    lcd_write_data8(0x00);
    lcd_write_cmd(0xED);
    lcd_write_data8(0x64);
    lcd_write_data8(0x03);
    lcd_write_data8(0x12);
    lcd_write_data8(0x81);
    lcd_write_cmd(0xF7);
    lcd_write_data8(0x20);
    lcd_write_cmd(0xC0);
    lcd_write_data8(0x23);
    lcd_write_cmd(0xC1);
    lcd_write_data8(0x10);
    lcd_write_cmd(0xC5);
    lcd_write_data8(0x3E);
    lcd_write_data8(0x28);
    lcd_write_cmd(0xC7);
    lcd_write_data8(0x86);
    lcd_write_cmd(0x36);
    lcd_write_data8(0x28);
    lcd_write_cmd(0x3A);
    lcd_write_data8(0x55);
    lcd_write_cmd(0xB1);
    lcd_write_data8(0x00);
    lcd_write_data8(0x18);
    lcd_write_cmd(0xB6);
    lcd_write_data8(0x08);
    lcd_write_data8(0x82);
    lcd_write_data8(0x27);
    lcd_write_cmd(0xF2);
    lcd_write_data8(0x00);
    lcd_write_cmd(0x26);
    lcd_write_data8(0x01);
    lcd_write_cmd(0xE0);
    lcd_write_data8(0x0F);
    lcd_write_data8(0x31);
    lcd_write_data8(0x2B);
    lcd_write_data8(0x0C);
    lcd_write_data8(0x0E);
    lcd_write_data8(0x08);
    lcd_write_data8(0x4E);
    lcd_write_data8(0xF1);
    lcd_write_data8(0x37);
    lcd_write_data8(0x07);
    lcd_write_data8(0x10);
    lcd_write_data8(0x03);
    lcd_write_data8(0x0E);
    lcd_write_data8(0x09);
    lcd_write_data8(0x00);
    lcd_write_cmd(0xE1);
    lcd_write_data8(0x00);
    lcd_write_data8(0x0E);
    lcd_write_data8(0x14);
    lcd_write_data8(0x03);
    lcd_write_data8(0x11);
    lcd_write_data8(0x07);
    lcd_write_data8(0x31);
    lcd_write_data8(0xC1);
    lcd_write_data8(0x48);
    lcd_write_data8(0x08);
    lcd_write_data8(0x0F);
    lcd_write_data8(0x0C);
    lcd_write_data8(0x31);
    lcd_write_data8(0x36);
    lcd_write_data8(0x0F);
    lcd_write_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_write_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void lcd_backlight_init(void)
{
    gpio_config_t c = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_BLK),
        .pull_down_en = 0,
        .pull_up_en = 0};
    gpio_config(&c);
    gpio_set_level(TFT_BLK, 1);
}

void lv_port_disp_init(void)
{
    ESP_LOGI(TAG, "=== LV_PORT_DISP v5 60FPS ===");

    gpio_config_t io = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_DC) | (1ULL << TFT_RST),
        .pull_down_en = 0,
        .pull_up_en = 0};
    gpio_config(&io);
    lcd_backlight_init();

    /* SPI bus with DMA */
    spi_bus_config_t bc = {
        .mosi_io_num = TFT_MOSI,
        .miso_io_num = TFT_MISO,
        .sclk_io_num = TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = HOR_RES * 2 /* one full row */
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bc, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dc = {
        .mode = 0,
        .clock_speed_hz = LCD_SPI_SPEED_HZ,
        .spics_io_num = TFT_CS,
        .queue_size = 1};
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dc, &lcd_spi));

    lcd_init_hw();
    ESP_LOGI(TAG, "ILI9341 %dx%d @ %dMHz DMA", HOR_RES, VER_RES,
             (int)(LCD_SPI_SPEED_HZ / 1000000));

    /* Full framebuffer in PSRAM */
    size_t fb_size = HOR_RES * VER_RES * 2; /* 153.6 KB */
    fb = (uint8_t *)heap_caps_malloc(fb_size,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(fb);

    lv_display_t *disp = lv_display_create(HOR_RES, VER_RES);

    /* KEY: RGB565_SWAPPED = big-endian byte order → no byte-swap in flush! */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);

    lv_display_set_buffers(disp, fb, NULL, fb_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, disp_flush_cb);
}

/* Stub for LV_SYSMON when LV_USE_OS=0 */
uint32_t lv_sysmon_get_idle_stub(void)
{
    return 0;
}