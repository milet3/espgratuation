/* ============================================================
 * touch_driver.c - XPT2046 on dedicated SPI3 bus (sensitivity tuned)
 *
 * Pins (app_config.h):
 *   TOUCH_SCLK = GPIO 4
 *   TOUCH_MOSI = GPIO 5
 *   TOUCH_MISO = GPIO 6
 *   TOUCH_CS   = GPIO 10
 *   TOUCH_IRQ  = GPIO 11 (unused)
 * ============================================================ */
#include "touch_driver.h"
#include "app_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "XPT2046"

/* ====== Sensitivity Tuning ====== */
#define TOUCH_SAMPLES       2    /* Number of samples to average (1-8) */
#define PRESSURE_THRESHOLD  15   /* Minimum Z pressure, lower=more sensitive */
#define TOUCH_SPI_SPEED_HZ  1500000  /* 2 MHz (XPT2046 max) */

/* XPT2046 commands (differential, 12-bit) */
#define CMD_X  0x90
#define CMD_Y  0xD0
#define CMD_Z1 0xB0
#define CMD_Z2 0xC0

static spi_device_handle_t touch_spi;

/* -- Helper: send a 3-byte command, receive 3 bytes -- */
static esp_err_t xpt_read_cmd(uint8_t cmd, uint8_t *rx)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    spi_transaction_t t = {
        .length    = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(touch_spi, &t);
}

void touch_driver_init(void)
{
    /* Diagnostic: verify GPIOs are not already claimed */
    ESP_LOGI(TAG, "Probing SPI3 pins: SCLK=%d MOSI=%d MISO=%d CS=%d",
             TOUCH_SCLK, TOUCH_MOSI, TOUCH_MISO, TOUCH_CS);

    /* Init SPI3 bus -- no DMA for short 3-byte transfers */
    spi_bus_config_t bus_cfg = {
        .mosi_io_num      = TOUCH_MOSI,
        .miso_io_num      = TOUCH_MISO,
        .sclk_io_num      = TOUCH_SCLK,
        .quadwp_io_num    = -1,
        .quadhd_io_num    = -1,
        .max_transfer_sz  = 64,
    };

    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI3 bus init FAILED: %s", esp_err_to_name(ret));
        return;
    }

    spi_device_interface_config_t dev_cfg = {
        .mode            = 0,
        .clock_speed_hz  = TOUCH_SPI_SPEED_HZ,
        .spics_io_num    = TOUCH_CS,
        .queue_size      = 1,
        /* .flags = 0 */
    };
    ret = spi_bus_add_device(SPI3_HOST, &dev_cfg, &touch_spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI3 add device FAILED: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "SPI3 bus initialized, %d MHz, samples=%d",
             (int)(TOUCH_SPI_SPEED_HZ / 1000000), TOUCH_SAMPLES);

    /* Wake XPT2046 from power-down with a dummy read */
    uint8_t rx[3];
    xpt_read_cmd(CMD_X, rx);
    vTaskDelay(pdMS_TO_TICKS(1));

    /* Diagnostic: read Z1 then Z2 to verify controller presence */
    xpt_read_cmd(CMD_Z1, rx);
    uint16_t z1 = ((uint16_t)(rx[1]) << 8 | rx[2]) >> 3;

    xpt_read_cmd(CMD_Z2, rx);
    uint16_t z2 = ((uint16_t)(rx[1]) << 8 | rx[2]) >> 3;

    if (z1 == 0 && z2 == 0) {
        ESP_LOGW(TAG, "Z1=Z2=0 -- touch controller NOT detected!");
        ESP_LOGW(TAG, "Check: 3.3V power, GND, and GPIO %d/%d/%d/%d wiring.",
                 TOUCH_SCLK, TOUCH_MOSI, TOUCH_MISO, TOUCH_CS);
    } else {
        ESP_LOGI(TAG, "Touch controller OK (Z1=%u, Z2=%u)", z1, z2);
    }
}

bool touch_get_xy(uint16_t *x, uint16_t *y, bool *pressed)
{
    static int call_count = 0;
    static uint16_t last_x = 0, last_y = 0;
    static int16_t pressure_sum = 0;
    call_count++;

    if (touch_spi == NULL) {
        *pressed = false;
        return false;
    }

    uint8_t rx[3];
    uint32_t sum_x = 0, sum_y = 0, sum_z = 0;
    uint8_t valid_samples = 0;

    /* Multi-sample averaging for noise reduction */
    for (int i = 0; i < TOUCH_SAMPLES; i++) {
        /* Read X */
        esp_err_t ret = xpt_read_cmd(CMD_X, rx);
        if (ret != ESP_OK) continue;
        uint16_t raw_x = ((uint16_t)(rx[1]) << 8 | rx[2]) >> 3;
        if (raw_x == 0 || raw_x >= 4095) continue;

        /* Read Y */
        ret = xpt_read_cmd(CMD_Y, rx);
        if (ret != ESP_OK) continue;
        uint16_t raw_y = ((uint16_t)(rx[1]) << 8 | rx[2]) >> 3;
        if (raw_y == 0 || raw_y >= 4095) continue;

        /* Read Z1/Z2 for pressure detection */
        ret = xpt_read_cmd(CMD_Z1, rx);
        if (ret != ESP_OK) continue;
        uint16_t z1 = ((uint16_t)(rx[1]) << 8 | rx[2]) >> 3;

        ret = xpt_read_cmd(CMD_Z2, rx);
        if (ret != ESP_OK) continue;
        uint16_t z2 = ((uint16_t)(rx[1]) << 8 | rx[2]) >> 3;

        /* Pressure = Rx-plate resistance, simplified */
        uint16_t pressure = (z1 > 0)
            ? (uint16_t)((uint32_t)z2 * raw_x / z1)
            : 0;

        sum_x += raw_x;
        sum_y += raw_y;
        sum_z += pressure;
        valid_samples++;
    }

    if (valid_samples == 0) {
        *pressed = false;
        *x = last_x;
        *y = last_y;
        return false;
    }

    uint16_t avg_x = (uint16_t)(sum_x / valid_samples);
    uint16_t avg_y = (uint16_t)(sum_y / valid_samples);
    uint16_t avg_pressure = (uint16_t)(sum_z / valid_samples);

    /* Store last valid position */
    last_x = avg_x;
    last_y = avg_y;

    *x = avg_x;
    *y = avg_y;

    /* Pressure-based touch detection */
    *pressed = (avg_pressure > PRESSURE_THRESHOLD);

    /* Smooth pressure for debug logging */
    pressure_sum = (pressure_sum * 7 + (int16_t)avg_pressure) / 8;

    if (call_count % 200 == 1) {
        ESP_LOGI(TAG, "raw=(%u,%u) pressure=%u (avg=%d) pressed=%d samples=%u",
                 avg_x, avg_y, avg_pressure, pressure_sum, *pressed, valid_samples);
    }

    return true;
}
