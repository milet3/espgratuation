#include "IIC_SENSOR.h"
#include "app_config.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lora.h"
#include "wifi_cat1.h"
#include <esp_log.h>
#include <stdio.h>

// 实例化全局变量 info
Info_CB info;
Sys_CB SysCB;

// 运行期统计信息
OTA_ZC_Stats g_ota_zc_stats = {0};
char DeviceNameBuff[SUN_NUMBER + 1][64] = {"GW001", "D001", "D002", "D003"};
char PorductIdBuff[SUN_NUMBER + 1][64] = {GW_PRODUCTID, SUB_PRODUCTID,
                                          SUB_PRODUCTID, SUB_PRODUCTID};

// 运行指示灯任务 (心跳灯)
void led_run_task(void *pvParameters) {
  while (1) {
    gpio_set_level(LED_RUN_PIN, 0); // 点亮
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(LED_RUN_PIN, 1); // 熄灭
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void app_main(void) {
  // 调用移植过来的硬件初始化代码
  bsp_led_init();
  bsp_key_init();

  // 启动传感器采集后台任务 (BH1750 & SHT30)
  iic_sensor_task_start();

  // 创建心跳灯任务
  xTaskCreate(led_run_task, "led_run_task", 2048, NULL, 5, NULL);

  // 初始化 NVS
  EEprom_Init();

  // =====================================
  // 初始化外设 (引脚在 app_config.h 中定义)
  // =====================================

  // 1. 初始化 LoRa LLCC68 (SPI 接口)
  LoRa_Init();

  // 2. 初始化 4G Cat1 模块 (UART 接口 + PPPoS)
  Cat1_PPPoS_Init();

  // 测试发送
  LoRa_SendData((uint8_t *)"Hello LoRa SPI", 14);
  bsp_uart_cat1_send("AT\r\n", 4);

  // NVS 测试
  ESP_LOGI("MAIN", "Testing NVS...");
  int32_t write_val = 12345;
  int32_t read_val = 0;

  // 测试写入键值对
  ESP_LOGI("MAIN", "Writing %ld to NVS...", write_val);
  EEprom_WriteData("threshold", &write_val, sizeof(write_val));

  // 测试读取键值对
  EEprom_ReadData("threshold", &read_val, sizeof(read_val));
  ESP_LOGI("MAIN", "Read from NVS: %ld", read_val);

  if (write_val == read_val) {
    ESP_LOGI("MAIN", "NVS Test Passed!");
  } else {
    ESP_LOGE("MAIN", "NVS Test Failed!");
  }

  // 传感器数据读取测试
  iic_sensor_data_t sensor_data;
  while (1) {
    iic_sensor_get_data(&sensor_data);
    ESP_LOGI("MAIN",
             "Sensor Data -> Temp: %.2f C, Hum: %.2f %%, Light: %.2f lux",
             sensor_data.temperature, sensor_data.humidity, sensor_data.lux);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void start_OTAWriteTask(void *pvParameters) {
  // 如果没有被创建，这里初始化一次（通常可以放在 main 里初始化）
  if (OTA_ZC_Queue == NULL) {
    OTA_ZC_Queue = xQueueCreate(OTA_ZC_QUEUE_LEN, sizeof(OTA_ZC_Chunk *));
  }

  OTA_ZC_Chunk *chunk = NULL;

  ESP_LOGI("OTA", "Zero-Copy OTA Write Task Started");

  for (;;) {
    // 1. 阻塞等待队列中传来指针
    if (xQueueReceive(OTA_ZC_Queue, &chunk, portMAX_DELAY) == pdTRUE) {
      if (chunk == NULL)
        continue;

      // 根据 ota_staflag 决定写入的分区名称
      // 0 写网关自己 -> APP 类型的 "ota_0"
      // 1 写子设备   -> APP 类型的 "ota_1" (或者您定义的数据分区)
      const char *part_label = (chunk->ota_staflag == 1) ? "ota_1" : "ota_0";

      // ESP32 的 ota_0 和 ota_1 默认都是 APP 分区类型
      const esp_partition_t *part = esp_partition_find_first(
          ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, part_label);

      if (part != NULL) {
        esp_err_t err = ESP_FAIL;
        // 注意：page_index 是基于页/分片的偏移，如果每一片大小是 256 字节：
        uint32_t write_offset = chunk->page_index * OTA_RANGE_SIZE;

        // 2. 带重试机制的 Flash 写入
        for (int attempt = 0; attempt < OTA_ZC_WRITE_RETRY_MAX; attempt++) {
          err =
              esp_partition_write(part, write_offset, chunk->data, chunk->len);
          if (err == ESP_OK) {
            g_ota_zc_stats.processed++;
            break;
          }
          g_ota_zc_stats.write_retry++;
          vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (err != ESP_OK) {
          g_ota_zc_stats.write_fail++;
          ESP_LOGE("OTA", "写入分区 %s 失败! offset=0x%08X", part_label,
                   (unsigned int)write_offset);
        } else {
          ESP_LOGD("OTA", "写入成功: offset=0x%08X, len=%d",
                   (unsigned int)write_offset, chunk->len);
        }
      } else {
        ESP_LOGE("OTA", "未找到目标分区 %s", part_label);
      }

      // 3. 【核心】释放这块动态内存（包含了结构体本身和后面的 payload）
      free(chunk);
    }
  }
}
