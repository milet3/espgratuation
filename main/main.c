#include "IIC_SENSOR.h"
#include "app_config.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "esp_mqtt.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "k210.h"
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
void led_run_task(void *pvParameters);
// MQTT 数据上报任务
void mqtt_upload_task(void *pvParameters);
// OTA 写入任务
void start_OTAWriteTask(void *pvParameters);

// 运行指示灯任务 (心跳灯)
void led_run_task(void *pvParameters) {
  while (1) {
    gpio_set_level(LED_RUN_PIN, 0); // 点亮
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(LED_RUN_PIN, 1); // 熄灭
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// MQTT 数据上报任务
void mqtt_upload_task(void *pvParameters) {
  iic_sensor_data_t sensor_data;
  while (1) {
    // 检查是否连接上 MQTT
    if (SysCB.SysEventFlag & CONNECT_MQTT) {
      // 获取传感器数据
      iic_sensor_get_data(&sensor_data);
      ESP_LOGI("UPLOAD", "Uploading Sensor Data...");

      // 上传数据到 OneNet
      WiFi_Cat1_GatewayDataPost(sensor_data.temperature, sensor_data.humidity,
                                sensor_data.lux);
    } else {
      ESP_LOGW("UPLOAD", "MQTT not connected, skipping upload...");
    }

    // 每 10 秒上传一次（可根据需要调整）
    vTaskDelay(pdMS_TO_TICKS(10000));
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

  // 创建数据上传任务
  xTaskCreate(mqtt_upload_task, "mqtt_upload_task", 4096, NULL, 5, NULL);

  // 初始化 NVS
  EEprom_Init();

  // =====================================
  // 初始化外设 (引脚在 app_config.h 中定义)
  // =====================================

  // 1. 初始化 LoRa LLCC68 (SPI 接口)
  LoRa_Init();

  // 2. 初始化 4G Cat1 模块 (UART 接口 + PPPoS)
  Cat1_PPPoS_Init();

  // 3. 启动 MQTT 客户端
  // 根据 app_config.h 中的定义拼接 URI
  char mqtt_uri[64];
  snprintf(mqtt_uri, sizeof(mqtt_uri), "mqtt://%s:%d", MQTT_SERVER, MQTT_PORT);
  esp_mqtt_app_start(mqtt_uri);

  // 4. 初始化 K210 视觉模块 (UART 接口)
  k210_uart_init();

  // 测试发送
  LoRa_SendData((uint8_t *)"Hello LoRa SPI", 14);
  bsp_uart_cat1_send("AT\r\n", 4);

  // =====================================
  // 初始化 OTA 升级任务
  // =====================================
  if (OTA_ZC_Queue == NULL) {
    OTA_ZC_Queue = xQueueCreate(OTA_ZC_QUEUE_LEN, sizeof(OTA_ZC_Chunk *));
  }
  // 启动 OTA 写入任务 (负责将下载的固件写入 Flash 分区)
  xTaskCreate(start_OTAWriteTask, "ota_write_task", 4096, NULL, 5, NULL);
  ESP_LOGI("MAIN", "OTA 升级任务已启动");

  // 传感器数据本地查看
  iic_sensor_data_t sensor_data;
  while (1) {
    iic_sensor_get_data(&sensor_data);
    ESP_LOGI("MAIN",
             "Local View -> Temp: %.2f C, Hum: %.2f %%, Light: %.2f lux",
             sensor_data.temperature, sensor_data.humidity, sensor_data.lux);
    vTaskDelay(pdMS_TO_TICKS(10000));
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

        // 如果是第一片数据，先擦除整个分区
        if (chunk->page_index == 0) {
          ESP_LOGI("OTA", "正在擦除分区 %s (%d 字节)...", part_label,
                   (int)part->size);
          esp_partition_erase_range(part, 0, part->size);
        }

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

        // 如果是最后一片数据，且是网关自身升级，则切换分区并重启
        if (chunk->is_last && chunk->ota_staflag == 0) {
          ESP_LOGI("OTA", "网关升级完成，正在切换分区并重启...");
          esp_ota_set_boot_partition(part);
          vTaskDelay(pdMS_TO_TICKS(1000));
          esp_restart();
        }
      } else {
        ESP_LOGE("OTA", "未找到目标分区 %s", part_label);
      }

      // 3. 【核心】释放这块动态内存（包含了结构体本身和后面的 payload）
      free(chunk);
    }
  }
}
