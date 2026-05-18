#include "soil_sensor.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SOIL_SENSOR";

/**
 * @brief Modbus CRC16 计算函数
 */
static uint16_t modbus_crc16(uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return (crc << 8) | (crc >> 8); // 转换为大端
}

esp_err_t soil_sensor_init(void) {
  // 0. 初始化电源和地引脚
  gpio_config_t io_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask =
          (1ULL << SOIL_UART_POWER_PIN) | (1ULL << SOIL_UART_GND_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&io_conf);

  // 设置电源引脚为高，地引脚为低
  gpio_set_level(SOIL_UART_POWER_PIN, 1);
  gpio_set_level(SOIL_UART_GND_PIN, 0);

  // 给传感器一点启动时间
  vTaskDelay(pdMS_TO_TICKS(500));

  uart_config_t uart_config = {
      .baud_rate = 9600, // 土壤传感器使用 9600
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // 1. 配置串口参数
  ESP_ERROR_CHECK(uart_param_config(SOIL_UART_PORT, &uart_config));

  // 2. 设置串口引脚
  ESP_ERROR_CHECK(uart_set_pin(SOIL_UART_PORT, SOIL_TX_PIN, SOIL_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  // 3. 安装串口驱动
  esp_err_t err = uart_driver_install(SOIL_UART_PORT, 256, 0, 0, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Soil UART driver install failed");
    return err;
  }

  ESP_LOGI(TAG, "Soil Sensor initialized on TX:%d RX:%d at 9600 bps",
           SOIL_TX_PIN, SOIL_RX_PIN);
  return ESP_OK;
}

esp_err_t soil_sensor_read_data(soil_sensor_data_t *data) {
  uint8_t cmd[8] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
  uint16_t crc = modbus_crc16(cmd, 6);
  cmd[6] = (uint8_t)(crc >> 8);
  cmd[7] = (uint8_t)(crc & 0xFF);

  // 清空接收缓存
  uart_flush(SOIL_UART_PORT);

  // 发送指令
  uart_write_bytes(SOIL_UART_PORT, (const char *)cmd, 8);

  // 接收响应 (地址+功能码+字节数+16字节数据+2字节CRC = 21字节)
  uint8_t rx_buf[32];
  int len = uart_read_bytes(SOIL_UART_PORT, rx_buf, 21, pdMS_TO_TICKS(1000));

  if (len < 21) {
    ESP_LOGW(TAG, "Soil Sensor response timeout or length error: %d", len);
    if (len > 0) {
      ESP_LOG_BUFFER_HEX("SOIL_RAW_FAIL", rx_buf, len); // 打印错误时的原始数据
    }
    return ESP_FAIL;
  }

  // 校验 CRC
  uint16_t rx_crc = (rx_buf[len - 2] << 8) | rx_buf[len - 1];
  uint16_t cal_crc = modbus_crc16(rx_buf, len - 2);
  if (rx_crc != cal_crc) {
    ESP_LOGE(TAG, "Soil Sensor CRC check failed! RX:0x%04X, CAL:0x%04X", rx_crc,
             cal_crc);
    return ESP_FAIL;
  }

  // 解析数据 (大端模式转换)
  // rx_buf[3,4] 温度, rx_buf[5,6] 湿度, ...
  int16_t raw_temp = (int16_t)((rx_buf[3] << 8) | rx_buf[4]);
  uint16_t raw_humi = (uint16_t)((rx_buf[5] << 8) | rx_buf[6]);
  uint16_t raw_ec = (uint16_t)((rx_buf[7] << 8) | rx_buf[8]);
  uint16_t raw_sal = (uint16_t)((rx_buf[9] << 8) | rx_buf[10]);
  uint16_t raw_n = (uint16_t)((rx_buf[11] << 8) | rx_buf[12]);
  uint16_t raw_p = (uint16_t)((rx_buf[13] << 8) | rx_buf[14]);
  uint16_t raw_k = (uint16_t)((rx_buf[15] << 8) | rx_buf[16]);
  uint16_t raw_ph = (uint16_t)((rx_buf[17] << 8) | rx_buf[18]);

  // 根据传感器协议进行缩放 (通常温度、湿度、PH有10倍或100倍缩放)
  data->temperature = (float)raw_temp / 10.0;
  data->humidity = (float)raw_humi / 10.0;
  data->ec = (float)raw_ec;
  data->salinity = (float)raw_sal;
  data->nitrogen = (float)raw_n;
  data->phosphorus = (float)raw_p;
  data->potassium = (float)raw_k;
  data->ph = (float)raw_ph / 10.0;

  ESP_LOGI(TAG, "Read Success: T:%.1f, H:%.1f, EC:%d, PH:%.1f",
           data->temperature, data->humidity, (int)data->ec, data->ph);

  return ESP_OK;
}
