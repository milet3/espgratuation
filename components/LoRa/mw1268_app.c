#include "mw1268_app.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mw1268_uart.h"
#include "wifi_cat1.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "LORA_APP";
extern Sys_CB SysCB;

#define FRAME_HEADER_H 0xAAU
#define FRAME_HEADER_L 0x55U
#define CMD_REPORT 0x01U
#define CMD_QUERY 0x02U
#define CMD_CONTROL 0x03U

#define LORA_MIN_FRAME_LEN 5U
#define LORA_MAX_PAYLOAD_LEN UINT8_MAX
#define LORA_MAX_FRAME_LEN (LORA_MIN_FRAME_LEN + LORA_MAX_PAYLOAD_LEN)
#define LORA_RX_STAGING_SIZE 512U
#define NODE_REPORT_PAYLOAD_LEN 6U
#define NODE_OFFLINE_TIMEOUT_MS 25000U

static _LORA_DEVICE_STA lora_device_sta = LORA_RX_STA;
static uint8_t lora_rx_staging[LORA_RX_STAGING_SIZE];
static size_t lora_rx_staging_len = 0U;
static TickType_t lora_last_valid_frame_tick = 0;
static bool lora_initialized = false;

static uint8_t lora_crc8_maxim(const uint8_t *data, uint16_t len) {
  uint8_t crc = 0x00;

  while (len--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 0x80U) != 0U) {
        crc = (uint8_t)((crc << 1U) ^ 0x31U);
      } else {
        crc <<= 1U;
      }
    }
  }

  return crc;
}

static const char *lora_cmd_to_string(uint8_t cmd) {
  switch (cmd) {
  case CMD_REPORT:
    return "sensor report";
  case CMD_QUERY:
    return "status response";
  case CMD_CONTROL:
    return "control response";
  default:
    return "unknown";
  }
}

static void lora_gpio_init(void) {
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << LORA_MD0_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&io_conf);

  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << LORA_AUX_PIN);
  io_conf.pull_down_en = 1;
  gpio_config(&io_conf);
}

static void lora_drop_staging_bytes(size_t drop_len) {
  if (drop_len == 0U || lora_rx_staging_len == 0U) {
    return;
  }

  if (drop_len >= lora_rx_staging_len) {
    lora_rx_staging_len = 0U;
    return;
  }

  memmove(lora_rx_staging, lora_rx_staging + drop_len,
          lora_rx_staging_len - drop_len);
  lora_rx_staging_len -= drop_len;
}

static void lora_try_report_subdevice_online(const char *reason) {
  if ((SysCB.SysEventFlag & CONNECT_MQTT) == 0U) {
    return;
  }
  if ((SysCB.SysEventFlag & SUB_ONLINE_READY) != 0U) {
    return;
  }

  ESP_LOGI(TAG, "MQTT ready, reporting sub-device online after %s", reason);
  WiFi_Cat1_SubOnline(1, 1);
}

static void lora_mark_link_alive(const char *reason) {
  bool was_confirmed = (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) != 0U;

  lora_last_valid_frame_tick = xTaskGetTickCount();
  if (!was_confirmed) {
    SysCB.SysEventFlag |= SUB_LORA_CONFIRMED;
    ESP_LOGI(TAG, "LoRa link confirmed by %s", reason);
    lora_try_report_subdevice_online(reason);
  }
}

static void lora_check_link_timeout(void) {
  if ((SysCB.SysEventFlag & SUB_LORA_CONFIRMED) == 0U ||
      lora_last_valid_frame_tick == 0) {
    return;
  }

  uint32_t silent_ms =
      (uint32_t)((xTaskGetTickCount() - lora_last_valid_frame_tick) *
                 portTICK_PERIOD_MS);
  if (silent_ms < NODE_OFFLINE_TIMEOUT_MS) {
    return;
  }

  SysCB.SysEventFlag &= ~(SUB_LORA_CONFIRMED | SUB_NODE_DATA_READY);
  ESP_LOGW(TAG,
           "LoRa node timed out after %lu ms without a valid protocol frame",
           (unsigned long)silent_ms);
}

static esp_err_t lora_send_frame(uint8_t cmd, const uint8_t *data,
                                 uint8_t data_len) {
  uint8_t frame[LORA_MAX_FRAME_LEN];
  uint16_t frame_len = (uint16_t)(LORA_MIN_FRAME_LEN + data_len);

  if (data_len > 0U && data == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  frame[0] = FRAME_HEADER_H;
  frame[1] = FRAME_HEADER_L;
  frame[2] = cmd;
  frame[3] = data_len;

  if (data_len > 0U) {
    memcpy(&frame[4], data, data_len);
  }

  frame[4U + data_len] = lora_crc8_maxim(&frame[2], (uint16_t)(data_len + 2U));
  return LoRa_SendData(frame, frame_len) == 0 ? ESP_OK : ESP_FAIL;
}

static void lora_handle_report_frame(const uint8_t *payload, uint8_t data_len) {
  if (data_len != NODE_REPORT_PAYLOAD_LEN) {
    ESP_LOGW(TAG, "Unexpected sensor report length: %u", data_len);
    return;
  }

  uint16_t lux_val = (uint16_t)((payload[0] << 8) | payload[1]);
  int16_t temp_raw = (int16_t)((payload[2] << 8) | payload[3]);
  uint16_t humi_raw = (uint16_t)((payload[4] << 8) | payload[5]);

  SysCB.last_node_data.lightlux = (float)lux_val;
  SysCB.last_node_data.temperature = (float)temp_raw / 10.0f;
  SysCB.last_node_data.humidity = (float)humi_raw / 10.0f;
  SysCB.SysEventFlag |= SUB_NODE_DATA_READY;
  lora_mark_link_alive("sensor report");

  ESP_LOGI(TAG, "Node report: lux=%u lx temp=%.1f C humi=%.1f %%",
           (unsigned int)lux_val, (double)SysCB.last_node_data.temperature,
           (double)SysCB.last_node_data.humidity);
}

static void lora_handle_query_frame(const uint8_t *payload, uint8_t data_len) {
  if (data_len == 0U) {
    ESP_LOGW(TAG, "Node status response missing online flag payload");
    return;
  }

  if (payload[0] != 0x01U) {
    ESP_LOGW(TAG, "Node status response rejected, payload[0]=0x%02X",
             payload[0]);
    return;
  }

  lora_mark_link_alive("status response");
  ESP_LOGI(TAG, "Node status response confirmed online");
}

static void lora_handle_control_frame(const uint8_t *payload, uint8_t data_len) {
  if (data_len == 0U) {
    ESP_LOGI(TAG, "Node control response received without payload");
    return;
  }

  ESP_LOGI(TAG, "Node control response payload[0]=0x%02X", payload[0]);
}

static void lora_process_frame(const uint8_t *frame, size_t frame_len) {
  uint8_t cmd = frame[2];
  uint8_t data_len = frame[3];
  const uint8_t *payload = &frame[4];
  uint8_t crc_received = frame[frame_len - 1U];
  uint8_t crc_calc = lora_crc8_maxim(&frame[2], (uint16_t)(data_len + 2U));

  if (crc_calc != crc_received) {
    ESP_LOGW(TAG,
             "Discarding frame with CRC mismatch: cmd=0x%02X calc=0x%02X recv=0x%02X",
             cmd, crc_calc, crc_received);
    return;
  }

  ESP_LOGI(TAG, "Valid LoRa frame: cmd=0x%02X (%s), len=%u", cmd,
           lora_cmd_to_string(cmd), data_len);

  switch (cmd) {
  case CMD_REPORT:
    lora_handle_report_frame(payload, data_len);
    break;
  case CMD_QUERY:
    lora_handle_query_frame(payload, data_len);
    break;
  case CMD_CONTROL:
    lora_handle_control_frame(payload, data_len);
    break;
  default:
    ESP_LOGW(TAG, "Unsupported LoRa command: 0x%02X", cmd);
    break;
  }
}

static void lora_parse_rx_staging(void) {
  while (lora_rx_staging_len > 0U) {
    size_t header_pos = SIZE_MAX;

    for (size_t i = 0U; i + 1U < lora_rx_staging_len; ++i) {
      if (lora_rx_staging[i] == FRAME_HEADER_H &&
          lora_rx_staging[i + 1U] == FRAME_HEADER_L) {
        header_pos = i;
        break;
      }
    }

    if (header_pos == SIZE_MAX) {
      if (lora_rx_staging_len == 1U &&
          lora_rx_staging[0] == FRAME_HEADER_H) {
        return;
      }

      if (lora_rx_staging[lora_rx_staging_len - 1U] == FRAME_HEADER_H) {
        lora_rx_staging[0] = FRAME_HEADER_H;
        lora_rx_staging_len = 1U;
      } else {
        lora_rx_staging_len = 0U;
      }
      return;
    }

    if (header_pos > 0U) {
      ESP_LOGW(TAG, "Dropping %u noise bytes before LoRa frame header",
               (unsigned int)header_pos);
      lora_drop_staging_bytes(header_pos);
    }

    if (lora_rx_staging_len < LORA_MIN_FRAME_LEN) {
      return;
    }

    uint8_t payload_len = lora_rx_staging[3];
    size_t frame_len = LORA_MIN_FRAME_LEN + payload_len;
    if (frame_len > LORA_MAX_FRAME_LEN) {
      ESP_LOGW(TAG, "Invalid LoRa frame length: %u", (unsigned int)payload_len);
      lora_drop_staging_bytes(1U);
      continue;
    }

    if (lora_rx_staging_len < frame_len) {
      return;
    }

    lora_process_frame(lora_rx_staging, frame_len);
    lora_drop_staging_bytes(frame_len);
  }
}

esp_err_t LoRa_Init(void) {
  esp_err_t err = ESP_OK;

  ESP_LOGI(TAG, "Starting LoRa MW1268 initialization");

  lora_gpio_init();
  lora_rx_staging_len = 0U;
  lora_last_valid_frame_tick = 0;
  lora_initialized = false;
  SysCB.SysEventFlag &= ~(SUB_LORA_CONFIRMED | SUB_NODE_DATA_READY);

  err = lora_uart_init(115200);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize LoRa UART at 115200: %s",
             esp_err_to_name(err));
    return err;
  }
  gpio_set_level(LORA_MD0_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(200));

  if (lora_send_cmd("AT", "OK", 50) != 0) {
    uart_driver_delete(LORA_UART_PORT);
    err = lora_uart_init(9600);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to initialize LoRa UART at 9600: %s",
               esp_err_to_name(err));
      return err;
    }
    if (lora_send_cmd("AT", "OK", 50) != 0) {
      ESP_LOGE(TAG, "Unable to communicate with LoRa module at 115200 or 9600");
      return ESP_FAIL;
    }
  }

  ESP_LOGI(TAG, "Sending AT+DEFAULT for factory reset");
  lora_send_cmd("AT+DEFAULT", "OK", 200);
  vTaskDelay(pdMS_TO_TICKS(300));

  gpio_set_level(LORA_MD0_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(200));

  uart_driver_delete(LORA_UART_PORT);
  err = lora_uart_init(115200);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to reinitialize LoRa UART at 115200: %s",
             esp_err_to_name(err));
    return err;
  }

  lora_device_sta = LORA_RX_STA;
  lora_initialized = true;
  ESP_LOGI(TAG, "LoRa MW1268 ready (115200 bps, transparent mode, addr=0, ch=23)");
  return ESP_OK;
}

uint8_t LoRa_SendData(const uint8_t *data, uint16_t len) {
  if (data == NULL || len == 0U) {
    return 1;
  }
  if (!lora_initialized) {
    ESP_LOGW(TAG, "LoRa send rejected because module is not initialized");
    return 1;
  }

  ESP_LOGI(TAG, "LoRa TX %u bytes", (unsigned int)len);
  ESP_LOG_BUFFER_HEX(TAG, data, len);

  int written = uart_write_bytes(LORA_UART_PORT, (const char *)data, len);
  return written == (int)len ? 0 : 1;
}

void LoRa_QueryNodeOnline(void) {
  ESP_LOGI(TAG, "Sending LoRa node online query frame");
  if (lora_send_frame(CMD_QUERY, NULL, 0U) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send node online query frame");
  }
}

void LoRa_ControlNodeLED(uint8_t on_off) {
  uint8_t value = on_off ? 0x01U : 0x00U;

  ESP_LOGI(TAG, "Sending LoRa LED control frame: %s", on_off ? "ON" : "OFF");
  if (lora_send_frame(CMD_CONTROL, &value, 1U) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to send node LED control frame");
  }
}

void LoRa_ActiveEvent(void) {
  uint8_t rx_buf[256];
  int len = lora_uart_read_raw(rx_buf, sizeof(rx_buf), 10);

  if (len > 0) {
    size_t chunk_len = (size_t)len;

    ESP_LOG_BUFFER_HEX(TAG, rx_buf, len);

    if (lora_rx_staging_len + chunk_len > sizeof(lora_rx_staging)) {
      ESP_LOGW(TAG, "LoRa RX staging overflow, resetting parser state");
      lora_rx_staging_len = 0U;
    }

    memcpy(lora_rx_staging + lora_rx_staging_len, rx_buf, chunk_len);
    lora_rx_staging_len += chunk_len;
    lora_parse_rx_staging();
  }

  lora_check_link_timeout();
}
