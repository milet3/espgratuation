#include "k210.h"
#include "app_config.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"


static const char *TAG = "K210";

esp_err_t k210_uart_init(void) {
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // 1. 配置串口参数
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_K210, &uart_config));

  // 2. 设置串口引脚 (从 app_config.h 中读取 GPIO 定义)
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_K210, K210_TX_PIN, K210_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  // 3. 安装串口驱动
  // 分配较大的缓冲区用于视觉识别数据的突发接收
  esp_err_t err =
      uart_driver_install(UART_NUM_K210, UART_BUF_SIZE, 0, 0, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "K210 UART driver install failed");
    return err;
  }

  ESP_LOGI(TAG, "K210 UART initialized on TX:%d RX:%d at 115200 bps",
           K210_TX_PIN, K210_RX_PIN);
  return ESP_OK;
}

int k210_uart_send(const uint8_t *data, int len) {
  if (data == NULL || len <= 0) {
    return 0;
  }
  return uart_write_bytes(UART_NUM_K210, (const char *)data, len);
}

int k210_uart_read(uint8_t *buf, int len, uint32_t timeout_ms) {
  if (buf == NULL || len <= 0) {
    return 0;
  }
  return uart_read_bytes(UART_NUM_K210, buf, len, pdMS_TO_TICKS(timeout_ms));
}
