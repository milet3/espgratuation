#include "mw1268_app.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mw1268_uart.h"
#include "wifi_cat1.h"
#include <string.h>

static const char *TAG = "LORA_APP";
extern Sys_CB SysCB;

/* 协议常量定义 */
#define FRAME_HEADER_H 0xAA
#define FRAME_HEADER_L 0x55
#define CMD_REPORT 0x01  // 传感器数据上报
#define CMD_QUERY 0x02   // 主动询问节点在线状态
#define CMD_CONTROL 0x03 // 控制节点外设 (如 LED)

/* 设备状态 */
static _LORA_DEVICE_STA lora_device_sta = LORA_RX_STA;

/**
 * @brief  CRC8 校验算法 (与子设备一致)
 * 参数：Initial: 0x00, Poly: 0x31 (x8 + x5 + x4 + 1)
 */
static uint8_t Get_CRC8(uint8_t *ptr, uint16_t len) {
  uint8_t crc = 0x00;
  while (len--) {
    crc ^= *ptr++;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x31;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

/**
 * @brief  模块引脚初始化
 */
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

/**
 * @brief  LoRa 初始化
 */
void LoRa_Init(void) {
  ESP_LOGI(TAG,
           "Starting LoRa MW1268 Initialization (User Factory Reset Logic)...");

  // 1. 临时降低全局日志等级
  esp_log_level_set("*", ESP_LOG_WARN);

  lora_gpio_init();

  /* Step 1: 探测并连接模块 */
  // 先尝试 115200
  lora_uart_init(115200);
  gpio_set_level(LORA_MD0_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(200));

  if (lora_send_cmd("AT", "OK", 50) != 0) {
    // 如果 115200 失败，卸载驱动并尝试 9600
    uart_driver_delete(LORA_UART_PORT);
    lora_uart_init(9600);
    if (lora_send_cmd("AT", "OK", 50) != 0) {
      ESP_LOGE(TAG, "LoRa Communication Error: Failed to established "
                    "connection at 115200 or 9600!");
      esp_log_level_set("*", ESP_LOG_INFO);
      return;
    }
  }

  /* Step 2: 发送出厂重置指令 */
  ESP_LOGI(TAG, "Sending AT+DEFAULT for Factory Reset...");
  lora_send_cmd("AT+DEFAULT", "OK", 200);
  vTaskDelay(pdMS_TO_TICKS(300));

  /* Step 3: 按照出厂默认 115200 速率运行 */
  gpio_set_level(LORA_MD0_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(200));

  // 根据用户逻辑，DEFAULT 后模块波特率为 115200
  uart_driver_delete(LORA_UART_PORT);
  lora_uart_init(115200);

  // 等待 AUX 稳定 (如果引脚连接正常)
  // while(gpio_get_level(LORA_AUX_PIN));

  // 恢复日志输出
  esp_log_level_set("*", ESP_LOG_INFO);

  lora_device_sta = LORA_RX_STA;
  ESP_LOGI(TAG, "LoRa MW1268 Ready (User Defaults: 115200, ADDR:0, CH:23)");
}

/**
 * @brief  LoRa 数据发送 (网关通常只收，但也保留发送能力)
 */
uint8_t LoRa_SendData(uint8_t *data, uint16_t len) {
  if (data == NULL || len == 0)
    return 1;

  ESP_LOGI(TAG, "LoRa Gateway Sending %d bytes...", len);
  ESP_LOG_BUFFER_HEX(TAG, data, len);

  uart_write_bytes(LORA_UART_PORT, (const char *)data, len);
  return 0;
}

/**
 * @brief  向子节点发送在线查询指令
 * 格式：AA 55 02 00 [CRC]
 */
void LoRa_QueryNodeOnline(void) {
  uint8_t query_pkt[5];
  query_pkt[0] = FRAME_HEADER_H;
  query_pkt[1] = FRAME_HEADER_L;
  query_pkt[2] = CMD_QUERY;
  query_pkt[3] = 0x00;                       // 数据长度为 0
  query_pkt[4] = Get_CRC8(&query_pkt[2], 2); // 校验 CMD 和 LEN

  ESP_LOGI(TAG, ">>> LoRa: Querying Sub-Node Online Status...");
  LoRa_SendData(query_pkt, 5);
}

/**
 * @brief  控制子节点 LED
 * 格式：AA 55 03 01 [VALUE] [CRC]
 */
void LoRa_ControlNodeLED(uint8_t on_off) {
  uint8_t ctrl_pkt[6];
  ctrl_pkt[0] = FRAME_HEADER_H;
  ctrl_pkt[1] = FRAME_HEADER_L;
  ctrl_pkt[2] = CMD_CONTROL;
  ctrl_pkt[3] = 0x01;                      // 数据长度为 1
  ctrl_pkt[4] = on_off ? 0x01 : 0x00;      // 0x01点亮, 0x00熄灭
  ctrl_pkt[5] = Get_CRC8(&ctrl_pkt[2], 3); // 校验 CMD + LEN + DATA

  ESP_LOGI(TAG, ">>> LoRa: Sending LED Control (%s) to Sub-Node...",
           on_off ? "ON" : "OFF");
  LoRa_SendData(ctrl_pkt, 6);
}

/**
 * @brief  LoRa 主动事件处理 (轮询接收子节点上传的传感器数据)
 */
void LoRa_ActiveEvent(void) {
  uint8_t rx_buf[256];
  int len = lora_uart_read_raw(rx_buf, sizeof(rx_buf) - 1, 10);

  if (len > 0) {
    // 1. 打印原始十六进制，方便调试
    ESP_LOGI(TAG, "LoRa Recv Raw [%d]:", len);
    ESP_LOG_BUFFER_HEX(TAG, rx_buf, len);

    // 2. 判定是否为轻量级二进制帧 (AA 55 开头)
    if (len >= 5 && rx_buf[0] == FRAME_HEADER_H &&
        rx_buf[1] == FRAME_HEADER_L) {
      uint8_t cmd = rx_buf[2];
      uint8_t data_len = rx_buf[3];

      // 安全检查：防止长度溢出
      if (len < (4 + data_len + 1)) {
        ESP_LOGW(TAG, "Binary frame too short for declared length %d",
                 data_len);
        return;
      }

      uint8_t crc_received = rx_buf[4 + data_len];
      // 使用 CRC8_MAXIM 重新计算校验 (从 CMD 到 DATA 结束)
      uint8_t crc_calc = Get_CRC8(&rx_buf[2], data_len + 2);

      if (crc_calc == crc_received) {
        ESP_LOGI(TAG, "Binary Frame Valid! (CRC OK) CMD:0x%02X, DataLen:%d",
                 cmd, data_len);
      } else {
        ESP_LOGW(TAG,
                 "Binary Frame CRC Error! (Calc:0x%02X, Recv:0x%02X). "
                 "Proceeding anyway for debugging.",
                 crc_calc, crc_received);
      }

      // 3. 处理数据上报逻辑 (CMD = 0x01)
      if (cmd == CMD_REPORT) {
        // 升级版协议：支持 6 字节负载 (光照2 + 温度2 + 湿度2)
        if (data_len >= 6) {
          uint16_t lux_val = (rx_buf[4] << 8) | rx_buf[5];
          int16_t temp_raw = (int16_t)((rx_buf[6] << 8) | rx_buf[7]);
          uint16_t humi_raw = (uint16_t)((rx_buf[8] << 8) | rx_buf[9]);

          float temp_val = (float)temp_raw / 10.0f;
          float humi_val = (float)humi_raw / 10.0f;

          ESP_LOGI(
              TAG,
              ">>> Parsed Sensor Data: Light = %d lx, Temp = %.1f C, Humi = "
              "%.1f %%",
              lux_val, temp_val, humi_val);

          // 标记 LoRa 通信已确认
          if (!(SysCB.SysEventFlag & SUB_LORA_CONFIRMED)) {
            ESP_LOGW(TAG, "通过数据上报确认子设备 LoRa 通信正常");
            SysCB.SysEventFlag |= SUB_LORA_CONFIRMED;
          }

          // 4. 更新缓存
          SysCB.last_node_data.temperature = temp_val;
          SysCB.last_node_data.humidity = humi_val;
          SysCB.last_node_data.lightlux = (float)lux_val;
        }
        // 兼容旧版协议：仅 2 字节光照
        else if (data_len >= 2) {
          uint16_t lux_val = (rx_buf[4] << 8) | rx_buf[5];
          ESP_LOGI(TAG, ">>> Parsed Sensor Data (Legacy): Light = %d lx",
                   lux_val);

          if (!(SysCB.SysEventFlag & SUB_LORA_CONFIRMED)) {
            SysCB.SysEventFlag |= SUB_LORA_CONFIRMED;
          }

          SysCB.last_node_data.lightlux = (float)lux_val;
        }
      }
      // 4. 处理在线查询回复 (CMD = 0x02)
      else if (cmd == CMD_QUERY) {
        if (data_len >= 1 && rx_buf[4] == 0x01) {
          ESP_LOGW(TAG, ">>> [LoRa 确认] 子节点响应在线查询，通信正常！");
          SysCB.SysEventFlag |= SUB_LORA_CONFIRMED;

          // [新增] 如果 MQTT 已连接，立即触发 OneNET 子设备上线报备
          if ((SysCB.SysEventFlag & CONNECT_MQTT) &&
              !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
            ESP_LOGI(TAG, "MQTT 已就绪，立即执行 OneNET 子设备上线报备...");
            WiFi_Cat1_SubOnline(1, 1);
            SysCB.SysEventFlag |= SUB_ONLINE_READY;
          }
        }
      }
    } else {
      // 如果不是二进制帧，按普通字符串处理
      rx_buf[len] = 0;
      ESP_LOGI(TAG, "Gateway Received Text: %s", (char *)rx_buf);
    }
  }
}
