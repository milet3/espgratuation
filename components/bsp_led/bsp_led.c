#include "bsp_led.h"
#include "app_config.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "led_strip_encoder.h"

static const char *TAG = "BSP_LED";
static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;
static uint8_t s_led_pixel[3] = {0};

#define BSP_LED_RMT_RESOLUTION_HZ 10000000U
#define BSP_LED_TX_TIMEOUT_TICKS pdMS_TO_TICKS(100)

static esp_err_t bsp_led_flush(void) {
  if (s_led_chan == NULL || s_led_encoder == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  rmt_transmit_config_t tx_config = {
      .loop_count = 0,
  };

  esp_err_t err = rmt_transmit(s_led_chan, s_led_encoder, s_led_pixel,
                               sizeof(s_led_pixel), &tx_config);
  if (err != ESP_OK) {
    return err;
  }

  return rmt_tx_wait_all_done(s_led_chan, BSP_LED_TX_TIMEOUT_TICKS);
}

esp_err_t bsp_led_init(void) {
  if (LED_GW001_RGB_PIN < 0) {
    return ESP_OK;  /* pin not connected */
  }
  if (s_led_chan != NULL && s_led_encoder != NULL) {
    return bsp_led_set(false);
  }

  rmt_tx_channel_config_t tx_chan_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .gpio_num = LED_GW001_RGB_PIN,
      .mem_block_symbols = 64,
      .resolution_hz = BSP_LED_RMT_RESOLUTION_HZ,
      .trans_queue_depth = 4,
  };
  esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &s_led_chan);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(err));
    return err;
  }

  led_strip_encoder_config_t encoder_config = {
      .resolution = BSP_LED_RMT_RESOLUTION_HZ,
  };
  err = rmt_new_led_strip_encoder(&encoder_config, &s_led_encoder);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create LED strip encoder: %s",
             esp_err_to_name(err));
    return err;
  }

  err = rmt_enable(s_led_chan);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable RMT TX channel: %s", esp_err_to_name(err));
    return err;
  }

  err = bsp_led_set(false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to clear onboard RGB: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Onboard RGB initialized on IO%d", LED_GW001_RGB_PIN);
  return ESP_OK;
}

esp_err_t bsp_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue) {
  if (s_led_chan == NULL || s_led_encoder == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  // Most onboard addressable RGB LEDs on ESP32 boards use GRB byte order.
  s_led_pixel[0] = green;
  s_led_pixel[1] = red;
  s_led_pixel[2] = blue;
  return bsp_led_flush();
}

esp_err_t bsp_led_set(bool on) {
  if (on) {
    return bsp_led_set_rgb(LED_GW001_RGB_ON_RED, LED_GW001_RGB_ON_GREEN,
                           LED_GW001_RGB_ON_BLUE);
  }

  return bsp_led_set_rgb(0, 0, 0);
}
