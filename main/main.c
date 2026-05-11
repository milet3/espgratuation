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
// #include "k210.h"
// #include "lora.h"
#include "soil_sensor.h"
#include "wifi_cat1.h"
#include <esp_event.h>
#include <esp_log.h>
#include <stdio.h>

// 实例化全局变量 info
Info_CB info;
Sys_CB SysCB;

// 运行期统计信息
OTA_ZC_Stats g_ota_zc_stats = {0};
char DeviceNameBuff[SUN_NUMBER + 1][64] = {"GW001", "D001", "D002", "D003"};
char ProductIdBuff[SUN_NUMBER + 1][64] = {GW_PRODUCTID, SUB_PRODUCTID,
                                          SUB_PRODUCTID, SUB_PRODUCTID};

// 运行指示灯任务 (心跳灯)
void led_run_task(void *pvParameters);
// MQTT 数据上报任务
void mqtt_upload_task(void *pvParameters);
// OTA 写入任务
void start_OTAWriteTask(void *pvParameters);
// 土壤传感器数据上传任务
// void soil_sensor_task(void *pvParameters);
// K210 消息处理任务
// void k210_msg_task(void *pvParameters);
// LoRa 轮询任务
// void lora_poll_task(void *pvParameters);

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
  static bool version_reported =
      false; // 恢复：用于记录本次连接是否已报备版本号
  static bool ota_checked_once = false; // 增加：确保开机只主动查询一次 OTA

  while (1) {
    // 检查是否正在进行 OTA 升级
    if (SysCB.SysEventFlag & OTA_RUNNING) {
      ESP_LOGW("UPLOAD", "OTA 升级中，暂停数据上报以保障下载带宽...");
    }
    // 检查是否连接上 MQTT
    else if (SysCB.SysEventFlag & CONNECT_MQTT) {
      // 1. 如果是新连接，先报备一次版本号（仅报备一次）
      if (!version_reported) {
        // 增加延时至 5 秒，确保 MQTT 链路和订阅完全稳定后再进行上报
        vTaskDelay(pdMS_TO_TICKS(5000));

        // 新版 OneNET 规范：通过属性上报触发平台对版本的感知
        WiFi_Cat1_GatewayDataPost(25.1f, 15.1f, 83.33f);

        version_reported = true;
        // 报备后等待 3 秒再传输后续数据
        vTaskDelay(pdMS_TO_TICKS(3000));
      }

      ESP_LOGI("UPLOAD",
               "Uploading Combined Sensor Data (Reduced Frequency)...");
      // 优化：合并所有传感器数据一次性上报，减少 4G 网络拥塞和 MQTT Buffer 压力
      WiFi_Cat1_AllDataPost(25.1f, 15.1f, 83.33f, // 空气数据
                            26.5f, 35.2f, 400.0f, // 土壤温湿度/EC
                            50.0f, 15.0f, 90.0f,  // 土壤NPK
                            1.47f, 1.17f, 1.20f); // ADC 数据

      // ★ 核心修改：第一次成功上报数据后，主动触发一次 OTA 检查 ★
      if (!ota_checked_once) {
        vTaskDelay(pdMS_TO_TICKS(2000)); // 稍微等 2 秒，让平台处理完刚才的上报
        ESP_LOGI("UPLOAD",
                 ">>> 首次上报完成，开始主动执行 Studio OTA 检查流程...");
        Studio_OTA_CheckTask();  // 直接调用之前修改好的检查函数
        ota_checked_once = true; // 标记为已检查，后续不再重复触发
      }
    } else {
      version_reported = false; // 断开连接后重置标志位
      ESP_LOGW("UPLOAD", "MQTT not connected, skipping upload...");
    }

    // 优化：每 30 秒上传一次 (原 10s 太频繁，易导致 4G/PPP 链路拥塞)
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

/*
// 土壤传感器采集任务
void soil_sensor_task(void *pvParameters) {
  soil_sensor_data_t soil_data;
  char post_topic[128];
  char post_data[512];

  while (1) {
    if (soil_sensor_read_data(&soil_data) == ESP_OK) {
      if (SysCB.SysEventFlag & CONNECT_MQTT) {
        snprintf(post_topic, sizeof(post_topic),
                 "$sys/%s/%s/thing/property/post", GW_PRODUCTID, GW_DEVICENAME);

        // 构造 JSON 上报 6 项土壤数据 (使用 app_config.h 中的 ATTRIBUTE_SOIL_*
        // 宏)
        snprintf(post_data, sizeof(post_data),
                 "{\"id\":\"456\",\"version\":\"1.0\",\"params\":{"
                 "\"%s\":{\"value\":%.1f}," // 土壤温度
                 "\"%s\":{\"value\":%.1f}," // 土壤水分
                 "\"%s\":{\"value\":%.1f}," // 土壤电导率
                 "\"%s\":{\"value\":%.1f}," // 土壤氮
                 "\"%s\":{\"value\":%.1f}," // 土壤磷
                 "\"%s\":{\"value\":%.1f}"  // 土壤钾
                 "}}",
                 ATTRIBUTE_SOIL_TEMP, soil_data.temperature,
                 ATTRIBUTE_SOIL_HUMI, soil_data.humidity, ATTRIBUTE_SOIL_EC,
                 soil_data.ec, ATTRIBUTE_SOIL_N, soil_data.nitrogen,
                 ATTRIBUTE_SOIL_P, soil_data.phosphorus, ATTRIBUTE_SOIL_K,
                 soil_data.potassium);

        esp_mqtt_publish_msg(post_topic, post_data, strlen(post_data), 0, 0);
        ESP_LOGI("SOIL_TASK", "Soil Data Uploaded: %s", post_data);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(15000)); // 每 15 秒采集一次
  }
}
*/

/*
// K210 消息处理任务：监听 K210 串口发送的害虫识别指令
void k210_msg_task(void *pvParameters) {
  uint8_t rx_buf[128];
  while (1) {
    // 阻塞读取 K210 串口数据
    int len = k210_uart_read(rx_buf, sizeof(rx_buf) - 1, 100);
    if (len > 0) {
      rx_buf[len] = '\0';
      ESP_LOGI("K210_TASK", "Received from K210: %s", (char *)rx_buf);

      // 解析协议，例如 K210 发送 "PEST:1" 表示发现害虫
      if (strstr((char *)rx_buf, "PEST:1") != NULL) {
        ESP_LOGW("K210_TASK", "!!! PEST DETECTED !!!");
        k210_report_pest_status(1); // 上报害虫状态为 1
      } else if (strstr((char *)rx_buf, "PEST:0") != NULL) {
        k210_report_pest_status(0); // 上报害虫状态为 0
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
*/

/*
// LoRa 轮询任务：定期检查子节点状态并触发读取
void lora_poll_task(void *pvParameters) {
  while (1) {
    // 调用 LoRa 组件的主动事件处理函数
    LoRa_ActiveEvent();
    // 轮询间隔 (内部已有 3s 判断，但这里也需要适当延时释放 CPU)
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
*/

void app_main(void) {
  ESP_LOGE("FIRMWARE", "!!!!!!!! 当前固件版本号: V99.99 !!!!!!!!");
  // 0. 初始化基础系统组件
  ESP_ERROR_CHECK(
      esp_event_loop_create_default()); // 修复 esp-modem
                                        // 崩溃的关键：创建默认事件循环

  // 1. 初始化电源引脚并上电
  gpio_config_t power_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << EM_POWER_PIN), // (1ULL << LORA_POWER_PIN) |
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&power_conf);
  // gpio_set_level(LORA_POWER_PIN, 1); // LoRa 上电
  gpio_set_level(EM_POWER_PIN, 1); // 传感器/外部模块上电
  vTaskDelay(pdMS_TO_TICKS(100));  // 等待电压稳定

  // 2. 调用硬件初始化代码
  bsp_led_init();
  bsp_key_init();

  // 启动传感器采集后台任务 (BH1750 & SHT30)
  // iic_sensor_task_start();

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
  // LoRa_Init();

  // 2. 初始化 4G Cat1 模块 (UART 接口 + PPPoS)
  if (Cat1_PPPoS_Init() == ESP_OK) {
    // 启动 Cat1 后台辅助任务 (含 OTA 监控)
    xTaskCreate(start_Cat1Task, "cat1_task", 4096, NULL, 5, NULL);
    // 3. 启动 MQTT 客户端
    esp_mqtt_app_start(NULL);
  } else {
    ESP_LOGE("MAIN", "4G Cat1 模块初始化失败，跳过 MQTT 启动");
  }

  // 4. 初始化 K210 视觉模块 (UART 接口)
  // k210_uart_init();

  // 5. 初始化土壤传感器 (UART 接口)
  soil_sensor_init();

  // 创建土壤传感器采集任务 (暂时屏蔽真实采集，改用 mqtt_upload_task
  // 里的固定上报) xTaskCreate(soil_sensor_task, "soil_sensor_task", 4096, NULL,
  // 5, NULL);

  // 创建 K210 消息处理任务
  // xTaskCreate(k210_msg_task, "k210_msg_task", 4096, NULL, 5, NULL);

  // 创建 LoRa 轮询任务
  // xTaskCreate(lora_poll_task, "lora_poll_task", 4096, NULL, 5, NULL);

  // 测试发送
  // LoRa_SendData((uint8_t *)"Hello LoRa SPI", 14);
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
  /*
  iic_sensor_data_t sensor_data;
  while (1) {
    iic_sensor_get_data(&sensor_data);
    ESP_LOGI("MAIN",
             "Local View -> Temp: %.2f C, Hum: %.2f %%, Light: %.2f lux",
             sensor_data.temperature, sensor_data.humidity, sensor_data.lux);
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
  */
  while (1) {
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
        // 注意：page_index 是基于页/分片的偏移，每一片大小是 256 字节：
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

        // 如果是最后一片数据，则切换分区并重启
        if (chunk->is_last) {
          ESP_LOGI("OTA", "固件下载完成，正在切换分区并重启...");
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
