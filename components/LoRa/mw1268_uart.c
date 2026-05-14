#include "mw1268_uart.h"
#include "app_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "LORA_UART";

#define LORA_RX_BUF_SIZE 1024
static uint8_t lora_rx_buf[LORA_RX_BUF_SIZE];

/**
 * @brief  初始化 LoRa 模块所使用的串口
 */
esp_err_t lora_uart_init(uint32_t baudrate) {
  uart_config_t uart_config = {
      .baud_rate = baudrate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_LOGI(TAG, "Initializing LoRa UART on TX:%d RX:%d at %d bps", LORA_UART_TX,
           LORA_UART_RX, (int)baudrate);

  esp_err_t err =
      uart_driver_install(LORA_UART_PORT, LORA_RX_BUF_SIZE * 2, 0, 0, NULL, 0);
  if (err != ESP_OK)
    return err;

  err = uart_param_config(LORA_UART_PORT, &uart_config);
  if (err != ESP_OK)
    return err;

  err = uart_set_pin(LORA_UART_PORT, LORA_UART_TX, LORA_UART_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  return err;
}

/**
 * @brief  向 MW1268 发送指令并等待应答
 */
uint8_t lora_send_cmd(char *cmd, char *ack, uint16_t waittime) {
  uint8_t res = 0;
  int len;

  // 清空缓冲区
  uart_flush(LORA_UART_PORT);

  // 发送指令
  if (cmd) {
    char cmd_buf[256];
    snprintf(cmd_buf, sizeof(cmd_buf), "%s\r\n", cmd);
    uart_write_bytes(LORA_UART_PORT, cmd_buf, strlen(cmd_buf));
    ESP_LOGD(TAG, "Send CMD: %s", cmd);
  }

  if (ack && waittime) {
    uint32_t start_time = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(waittime * 10)) {
      len = uart_read_bytes(LORA_UART_PORT, lora_rx_buf, LORA_RX_BUF_SIZE - 1,
                            pdMS_TO_TICKS(10));
      if (len > 0) {
        lora_rx_buf[len] = 0;
        // 提升日志等级，确保在自检阶段能看到模块返回的任何原始字符
        ESP_LOGI(TAG, "Recv Raw: %s", (char *)lora_rx_buf);
        if (strstr((const char *)lora_rx_buf, ack)) {
          return 0; // 成功
        }
      }
    }
    res = 1; // 超时
  }

  return res;
}

/**
 * @brief  从串口读取原始数据
 */
int lora_uart_read_raw(uint8_t *buf, uint32_t len, uint32_t timeout_ms) {
  return uart_read_bytes(LORA_UART_PORT, buf, len, pdMS_TO_TICKS(timeout_ms));
}
