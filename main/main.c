#include "IIC_SENSOR.h"
#include "app_config.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
// #include "esp_mqtt.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// #include "k210.h"
#include "esp_mqtt.h"
#include "mw1268_app.h"
#include "soil_sensor.h"
#include "wifi_cat1.h"
#include "wifi_manager.h"
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <stdlib.h> // 引入 rand()
#include <time.h>   // 引入 time()

// 实例化全局变量 info
Info_CB info;
Sys_CB SysCB;

// 运行期统计信息
OTA_ZC_Stats g_ota_zc_stats = {0};
char DeviceNameBuff[SUN_NUMBER + 1][64] = {"GW001", "D001", "D002", "D003"};
char ProductIdBuff[SUN_NUMBER + 1][64] = {GW_PRODUCTID, SUB_PRODUCTID,
                                          SUB_PRODUCTID, SUB_PRODUCTID};

// 统一传感器数据上传任务
void unified_sensor_upload_task(void *pvParameters);
// OTA 写入任务
void start_OTAWriteTask(void *pvParameters);
// LoRa 轮询任务
void lora_poll_task(void *pvParameters);

/**
 * @brief CAT1 延迟启动管理任务
 * 逻辑：启动后先关闭 CAT1，等待 2 分钟，若 WiFi 未连接则启动 CAT1
 */
void cat1_delayed_start_task(void *pvParameters) {
  ESP_LOGI("MAIN", "CAT1 延迟启动管理任务已启动，进入 2 分钟观察期...");

  // 等待 120 秒
  vTaskDelay(pdMS_TO_TICKS(120000));

  // 检查是否已经通过 WiFi 连接成功
  if (SysCB.SysEventFlag & CONNECT_WIFI) {
    ESP_LOGI("MAIN",
             "★★★ WiFi 已连接成功，CAT1 模块将保持关闭状态以节省功耗 ★★★");
  } else {
    ESP_LOGW("MAIN",
             "！！！ WiFi 配网超时或未连接，正在激活 4G CAT1 备用链路 ！！！");

    // 1. 初始化 4G Cat1 模块串口
    if (Cat1_AT_Init() == ESP_OK) {
      // 2. 执行开机序列
      Cat1_Reset();

      // 3. 启动后台辅助任务
      xTaskCreate(start_Cat1Task, "cat1_task", 4096, NULL, 5, NULL);
      // 4. 启动 MQTT 监控任务
      xTaskCreate(Cat1_AT_Mqtt_Task, "at_mqtt_task", 8192, NULL, 5, NULL);
    } else {
      ESP_LOGE("MAIN", "4G Cat1 模块串口初始化失败");
    }
  }

  vTaskDelete(NULL);
}

// 统一传感器数据上传任务 (分三轮上报：网关空气 -> 网关土壤 -> 子节点数据)
void unified_sensor_upload_task(void *pvParameters) {
  soil_sensor_data_t soil_data;
  bool version_reported = false;

  // 初始延时，等待系统稳定
  vTaskDelay(pdMS_TO_TICKS(10000));

  while (1) {
    if (SysCB.SysEventFlag & OTA_RUNNING) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (SysCB.SysEventFlag & CONNECT_MQTT) {
      // 第一轮：上报网关空气数据
      float air_temp = 25.5f;
      float air_hum = 60.0f;
      float air_lux = 150.0f;
      ESP_LOGI("UPLOAD", "Round 1: Uploading Gateway Air Data...");
      WiFi_Cat1_GatewayDataPost(air_temp, air_hum, air_lux);
      vTaskDelay(pdMS_TO_TICKS(2000)); // 轮次间隔延时

      // 第二轮：上报网关土壤数据
      if (soil_sensor_read_data(&soil_data) == ESP_OK) {
        ESP_LOGI("UPLOAD", "Round 2: Uploading Gateway Soil Data...");
        WiFi_Cat1_SoilDataPost(soil_data.temperature, soil_data.humidity,
                               soil_data.ec, soil_data.nitrogen,
                               soil_data.phosphorus, soil_data.potassium);
      } else {
        ESP_LOGW("UPLOAD", "Round 2: Skip Soil Data (Read Failed)");
      }
      vTaskDelay(pdMS_TO_TICKS(2000)); // 轮次间隔延时

      // 第三轮：上报子节点数据 (从缓存读取)
      if (SysCB.last_node_data.lightlux > 0) { // 简单判断是否有有效缓存
        ESP_LOGI("UPLOAD", "Round 3: Uploading Sub-Node Data (Proxy)...");
        WiFi_Cat1_NodeDataPost(SysCB.last_node_data.temperature,
                               SysCB.last_node_data.humidity,
                               SysCB.last_node_data.lightlux);
      } else {
        ESP_LOGW("UPLOAD", "Round 3: Skip Node Data (No Cache)");
      }

      if (!version_reported) {
        version_reported = true;
      }
    } else {
      version_reported = false;
    }

    // 完成一整轮后的长延时 (例如每 30 秒执行一个完整周期)
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

// LoRa 轮询任务：定期检查子节点状态并触发读取
void lora_poll_task(void *pvParameters) {
  ESP_LOGI("LORA_TASK", "LoRa Poll Task Started");
  static uint32_t last_test_send = 0;
  static uint32_t last_query_send = 0;

  // 1. 启动初期，主动询问子节点是否在线
  vTaskDelay(pdMS_TO_TICKS(2000)); // 等待模块完全稳定
  LoRa_QueryNodeOnline();
  last_query_send = xTaskGetTickCount();

  while (1) {
    // 调用 LoRa 组件的主动事件处理函数 (接收)
    LoRa_ActiveEvent();

    // 如果还没确认 LoRa 通信，每 5 秒重试询问一次
    if (!(SysCB.SysEventFlag & SUB_LORA_CONFIRMED)) {
      if (xTaskGetTickCount() - last_query_send > pdMS_TO_TICKS(5000)) {
        ESP_LOGW("LORA_TASK", "未收到子节点响应，正在重试 LoRa 在线查询...");
        LoRa_QueryNodeOnline();
        last_query_send = xTaskGetTickCount();
      }
    }

    // 每 30 秒发送一次测试数据（已确认通信后放慢频率）
    if (xTaskGetTickCount() - last_test_send > pdMS_TO_TICKS(30000)) {
      if (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) {
        LoRa_SendData((uint8_t *)"LoRa Gateway Heartbeat\r\n", 24);
      }
      last_test_send = xTaskGetTickCount();
    }

    // 轮询间隔
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// 外部变量声明
extern Sys_CB SysCB;

/**
 * @brief WiFi 状态回调函数
 * 逻辑：当 WiFi 获取到 IP 后，自动启动 MQTT 连接
 */
void wifi_state_callback(wifi_state_t state) {
  if (state == WIFI_STATE_CONNECTED) {
    ESP_LOGI("MAIN", "WiFi 已就绪，正在启动 MQTT...");
    esp_mqtt_app_start(NULL);
  } else if (state == WIFI_STATE_DISCONNECTED) {
    ESP_LOGW("MAIN", "WiFi 已断开，MQTT 将自动尝试重连或由监控任务处理");
  }
}

void app_main(void) {
  ESP_LOGE("FIRMWARE", "!!!!!!!! 当前固件版本号: V99.99 !!!!!!!! ");

  // 1. 初始化 NVS (WiFi 驱动必须)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // 【新增】播种随机数，确保每次重启后的数据都是真实的随机波动
  srand(time(NULL));

  // 2. 初始化电源引脚并上电
  gpio_config_t power_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask =
          (1ULL << SOIL_UART_POWER_PIN) | (1ULL << SOIL_UART_GND_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&power_conf);

  // 传感器/外部模块上电
  // 土壤传感器供电 (IO10 VCC, IO9 GND)
  gpio_set_level(SOIL_UART_POWER_PIN, 1);
  gpio_set_level(SOIL_UART_GND_PIN, 0);
  // LoRa 模块已通过 3V3 常供电，无需 GPIO 控制

  vTaskDelay(pdMS_TO_TICKS(500)); // 增加延时至 500ms，确保传感器完全启动

  // 【核心修改】初始化 CAT1 GPIO 并强制拉低（关闭模块）
  WiFi_Cat1_InitGPIO();
  CAT1_POWER(0);
  ESP_LOGI("MAIN", "已预先关闭 4G CAT1 模块，优先等待 WiFi 配网...");

  // 2. 调用硬件初始化代码
  bsp_led_init();

  // 3. 初始化 WiFi 并启动 AP 配网服务
  wifi_manager_init();
  wifi_set_state_callback(wifi_state_callback); // 注册状态回调
  wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");

  // 暂时屏蔽 I2C 传感器采集任务 (SHT30/BH1750)，改用固定值测试
  // iic_sensor_task_start();

  // 创建统一传感器数据上传任务 (分轮次：网关空气 -> 网关土壤 -> 子节点数据)
  xTaskCreate(unified_sensor_upload_task, "sensor_upload", 8192, NULL, 5, NULL);

  // 初始化 NVS
  EEprom_Init();

  // 启动 CAT1 延迟启动管理任务 (2分钟后若无 WiFi 则启动 4G)
  xTaskCreate(cat1_delayed_start_task, "cat1_delay", 4096, NULL, 5, NULL);

  // =====================================
  // 初始化外设 (引脚在 app_config.h 中定义)
  // =====================================
  soil_sensor_init();

  // 1. 初始化 LoRa MW1268 (UART 接口)
  LoRa_Init();

  // 创建 LoRa 轮询任务
  xTaskCreate(lora_poll_task, "lora_poll_task", 4096, NULL, 5, NULL);

  // 移除初始化时的单次测试发送，已改为在任务中循环发送
  // bsp_uart_cat1_send("AT\r\n", 4); // 此时 CAT1 驱动未初始化，调用会导致错误

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
