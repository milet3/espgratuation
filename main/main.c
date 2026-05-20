#include "app_config.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mw1268_app.h"
#include "soil_sensor.h"
#include "wifi_cat1.h"
#include "wifi_manager.h"
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

Info_CB info;
Sys_CB SysCB;

OTA_ZC_Stats g_ota_zc_stats = {0};
char DeviceNameBuff[SUN_NUMBER + 1][64] = {"GW001", "D001", "D002", "D003"};
char ProductIdBuff[SUN_NUMBER + 1][64] = {GW_PRODUCTID, SUB_PRODUCTID,
                                          SUB_PRODUCTID, SUB_PRODUCTID};

static TaskHandle_t mqtt_start_task_handle = NULL;
static TaskHandle_t ota_bootstrap_task_handle = NULL;
static bool boot_saved_wifi_pending = false;
static bool boot_saved_wifi_ap_started = false;
static bool gateway_fw_reported = false;

static void app_configure_log_levels(void);
static void mqtt_start_task(void *pvParameters);
static void ota_bootstrap_task(void *pvParameters);
static void start_boot_saved_wifi_ap_fallback(void);
static void unified_sensor_upload_task(void *pvParameters);
static void lora_poll_task(void *pvParameters);
void WiFi_Cat1_ReportBootOtaResult(void);

static void app_configure_log_levels(void) {
  esp_log_level_set("*", ESP_LOG_WARN);

  esp_log_level_set("FIRMWARE", ESP_LOG_INFO);
  esp_log_level_set("MAIN", ESP_LOG_INFO);
  esp_log_level_set("WIFI_MANAGER", ESP_LOG_INFO);
  esp_log_level_set("ESP_MQTT", ESP_LOG_INFO);
  esp_log_level_set("WIFI_CAT1", ESP_LOG_INFO);
  esp_log_level_set("UPLOAD", ESP_LOG_INFO);
  esp_log_level_set("OTA", ESP_LOG_INFO);
  esp_log_level_set("LORA_TASK", ESP_LOG_WARN);
  esp_log_level_set("LORA_APP", ESP_LOG_WARN);
  esp_log_level_set("LORA_UART", ESP_LOG_WARN);
  esp_log_level_set("BSP_NVS", ESP_LOG_WARN);

  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);
  esp_log_level_set("esp_netif_lwip", ESP_LOG_WARN);
  esp_log_level_set("phy_init", ESP_LOG_WARN);
  esp_log_level_set("pp", ESP_LOG_WARN);
  esp_log_level_set("net80211", ESP_LOG_WARN);
  esp_log_level_set("mqtt_client", ESP_LOG_WARN);
  esp_log_level_set("transport_tcp", ESP_LOG_WARN);
  esp_log_level_set("transport", ESP_LOG_WARN);
  esp_log_level_set("outbox", ESP_LOG_WARN);
  esp_log_level_set("esp-tls", ESP_LOG_WARN);
  esp_log_level_set("mqtt_common", ESP_LOG_WARN);
}

static void mqtt_start_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(1000));
  ESP_LOGI("MAIN", "WiFi 已就绪，开始启动 MQTT");
  esp_mqtt_app_start(NULL);
  mqtt_start_task_handle = NULL;
  vTaskDelete(NULL);
}

static void ota_bootstrap_task(void *pvParameters) {
  const TickType_t wait_step = pdMS_TO_TICKS(500);
  const TickType_t wait_timeout = pdMS_TO_TICKS(30000);
  TickType_t waited = 0;

  ESP_LOGI("MAIN", "启动后 OTA 检查任务已启动");
  ESP_LOGI("MAIN", "ota_boot stack watermark=%u",
           (unsigned int)uxTaskGetStackHighWaterMark(NULL));

  while ((SysCB.SysEventFlag & CONNECT_WIFI) &&
         !(SysCB.SysEventFlag & CONNECT_MQTT) && waited < wait_timeout) {
    vTaskDelay(wait_step);
    waited += wait_step;
  }

  if ((SysCB.SysEventFlag & CONNECT_MQTT) && !gateway_fw_reported) {
    WiFi_Cat1_PropertyVersion(0);
    gateway_fw_reported = true;
    vTaskDelay(pdMS_TO_TICKS(1500));
  }

  if (SysCB.SysEventFlag & CONNECT_MQTT) {
    WiFi_Cat1_ReportBootOtaResult();
  }

  if ((SysCB.SysEventFlag & CONNECT_WIFI) && !(SysCB.SysEventFlag & OTA_RUNNING)) {
    Studio_OTA_CheckTask();
  }

  ota_bootstrap_task_handle = NULL;
  vTaskDelete(NULL);
}

static void start_boot_saved_wifi_ap_fallback(void) {
  if (!boot_saved_wifi_pending || boot_saved_wifi_ap_started) {
    return;
  }

  boot_saved_wifi_pending = false;
  boot_saved_wifi_ap_started = true;

  ESP_LOGW("MAIN", "历史 WiFi 连接失败，切换到 AP 配网模式");
  wifi_manager_cancel_connect_retry();
  esp_err_t err =
      wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");
  if (err != ESP_OK) {
    ESP_LOGE("MAIN", "启动 AP 配网失败: %s",
             esp_err_to_name(err));
  }
}

void cat1_delayed_start_task(void *pvParameters) {
  ESP_LOGI("MAIN", "CAT1 备用链路监测任务已启动");
  vTaskDelay(pdMS_TO_TICKS(120000));

  if (SysCB.SysEventFlag & CONNECT_WIFI) {
    ESP_LOGI("MAIN", "WiFi 已连接，保持 CAT1 关机");
  } else {
    ESP_LOGW("MAIN", "WiFi 不可用，启用 CAT1 备用链路");
    if (Cat1_AT_Init() == ESP_OK) {
      Cat1_Reset();
      xTaskCreate(start_Cat1Task, "cat1_task", 4096, NULL, 5, NULL);
      xTaskCreate(Cat1_AT_Mqtt_Task, "at_mqtt_task", 8192, NULL, 5, NULL);
    } else {
      ESP_LOGE("MAIN", "初始化 CAT1 模块失败");
    }
  }

  vTaskDelete(NULL);
}

static void unified_sensor_upload_task(void *pvParameters) {
  soil_sensor_data_t soil_data;

  vTaskDelay(pdMS_TO_TICKS(10000));

  while (1) {
    if (SysCB.SysEventFlag & OTA_RUNNING) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (SysCB.SysEventFlag & CONNECT_MQTT) {
      if (!gateway_fw_reported) {
        WiFi_Cat1_PropertyVersion(0);
        gateway_fw_reported = true;
        vTaskDelay(pdMS_TO_TICKS(1000));
      }

      ESP_LOGI("UPLOAD", "开始上传网关空气数据");
      WiFi_Cat1_GatewayDataPost(25.5f, 60.0f, 150.0f);
      vTaskDelay(pdMS_TO_TICKS(2000));

      if (soil_sensor_read_data(&soil_data) == ESP_OK) {
        ESP_LOGI("UPLOAD", "开始上传网关土壤数据");
        WiFi_Cat1_SoilDataPost(soil_data.temperature, soil_data.humidity,
                               soil_data.ec, soil_data.nitrogen,
                               soil_data.phosphorus, soil_data.potassium);
      } else {
        ESP_LOGW("UPLOAD", "土壤传感器读取失败，跳过土壤数据上传");
      }
      vTaskDelay(pdMS_TO_TICKS(2000));

      if (SysCB.last_node_data.lightlux > 0) {
        ESP_LOGI("UPLOAD", "开始上传子节点缓存数据");
        WiFi_Cat1_NodeDataPost(SysCB.last_node_data.temperature,
                               SysCB.last_node_data.humidity,
                               SysCB.last_node_data.lightlux);
      } else {
        ESP_LOGW("UPLOAD", "子节点缓存为空，跳过子节点数据上传");
      }
    } else {
      gateway_fw_reported = false;
    }

    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

static void lora_poll_task(void *pvParameters) {
  uint32_t last_test_send = 0;
  uint32_t last_query_send = 0;
  uint32_t lora_retry_count = 0;

  ESP_LOGI("LORA_TASK", "LoRa 轮询任务已启动");
  vTaskDelay(pdMS_TO_TICKS(2000));
  LoRa_QueryNodeOnline();
  last_query_send = xTaskGetTickCount();

  while (1) {
    LoRa_ActiveEvent();

    if (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) {
      lora_retry_count = 0;
    } else if (xTaskGetTickCount() - last_query_send > pdMS_TO_TICKS(5000)) {
      ++lora_retry_count;
      if (lora_retry_count == 1 || (lora_retry_count % 6U) == 0U) {
        ESP_LOGW("LORA_TASK",
                 "LoRa 子节点暂未响应，继续重试在线查询（第 %lu 次）",
                 (unsigned long)lora_retry_count);
      }
      LoRa_QueryNodeOnline();
      last_query_send = xTaskGetTickCount();
    }

    if (xTaskGetTickCount() - last_test_send > pdMS_TO_TICKS(30000)) {
      if (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) {
        LoRa_SendData((uint8_t *)"LoRa Gateway Heartbeat\r\n", 24);
      }
      last_test_send = xTaskGetTickCount();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void wifi_state_callback(wifi_state_t state) {
  if (state == WIFI_STATE_CONNECTED) {
    boot_saved_wifi_pending = false;
    gateway_fw_reported = false;

    if (mqtt_start_task_handle == NULL) {
      xTaskCreate(mqtt_start_task, "mqtt_start", 4096, NULL, 5,
                  &mqtt_start_task_handle);
    }
    if (ota_bootstrap_task_handle == NULL) {
      xTaskCreate(ota_bootstrap_task, "ota_boot", 12288, NULL, 3,
                  &ota_bootstrap_task_handle);
    }
  } else if (state == WIFI_STATE_DISCONNECTED) {
    gateway_fw_reported = false;
    ESP_LOGW("MAIN", "WiFi 已断开");
    start_boot_saved_wifi_ap_fallback();
  }
}

void app_main(void) {
  app_configure_log_levels();
  ESP_LOGI("FIRMWARE", "当前固件版本: %s",
           WiFi_Cat1_GetRuntimeFirmwareVersion());

  ESP_ERROR_CHECK(EEprom_Init());
  EEprom_ReadInfo();

  srand((unsigned int)time(NULL));

  gpio_config_t power_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask =
          (1ULL << SOIL_UART_POWER_PIN) | (1ULL << SOIL_UART_GND_PIN),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&power_conf);

  gpio_set_level(SOIL_UART_POWER_PIN, 1);
  gpio_set_level(SOIL_UART_GND_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(500));

  WiFi_Cat1_InitGPIO();
  CAT1_POWER(0);
  ESP_LOGI("MAIN", "系统启动：优先使用 WiFi，保持 CAT1 关机");

  bsp_led_init();
  wifi_manager_init();
  wifi_set_state_callback(wifi_state_callback);

  wifi_credentials_t saved_wifi = {0};
  ESP_LOGI("MAIN", "正在加载历史 WiFi 配置");
  if (wifi_manager_load_saved_config(&saved_wifi)) {
    ESP_LOGI("MAIN", "使用历史 WiFi 连接: %s", saved_wifi.ssid);
    boot_saved_wifi_pending = true;
    boot_saved_wifi_ap_started = false;
    wifi_manager_connect(saved_wifi.ssid, saved_wifi.password);
  } else {
    boot_saved_wifi_pending = false;
    boot_saved_wifi_ap_started = true;
    ESP_LOGI("MAIN", "未找到历史 WiFi，启动 AP 配网");
    wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");
  }

  xTaskCreate(unified_sensor_upload_task, "sensor_upload", 8192, NULL, 5, NULL);
  xTaskCreate(cat1_delayed_start_task, "cat1_delay", 4096, NULL, 5, NULL);

  soil_sensor_init();
  LoRa_Init();
  xTaskCreate(lora_poll_task, "lora_poll_task", 4096, NULL, 5, NULL);

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
