#include "app_config.h"
#include "mem_guard.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mqtt.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mw1268_app.h"
#include "soil_sensor.h"
#include "IIC_SENSOR.h"
#include "lvgl_ui.h"
#include "lvgl.h"
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
static void heap_monitor_task(void *pvParameters);
void WiFi_Cat1_ReportBootOtaResult(void);


/* Heap monitor - periodically logs DRAM / SPIRAM usage and stack high watermarks */
__attribute__((unused)) static void heap_monitor_task(void *pvParameters) {
  (void)pvParameters;

  /* External task handles used in watermark reporting */
  extern TaskHandle_t mqtt_start_task_handle;
  extern TaskHandle_t ota_bootstrap_task_handle;

  while (1) {
    /* Suppress logging when memory guard says so */
    if (!mem_guard_log_suppressed()) {
      uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      uint32_t min_free_internal =
          heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
      uint32_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
      uint32_t min_free_spiram =
          heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

      ESP_LOGI("HEAP",
               "DRAM free=%" PRIu32 " min=%" PRIu32 " | "
               "SPIRAM free=%" PRIu32 " min=%" PRIu32,
               free_internal, min_free_internal, free_spiram, min_free_spiram);

      /* Stack high watermarks for key tasks (printed only when handles exist) */
      if (mqtt_start_task_handle)
        ESP_LOGI("HEAP", "mqtt_start stack HWM=%u",
                 (unsigned int)uxTaskGetStackHighWaterMark(
                     mqtt_start_task_handle));
      if (ota_bootstrap_task_handle)
        ESP_LOGI("HEAP", "ota_boot   stack HWM=%u",
                 (unsigned int)uxTaskGetStackHighWaterMark(
                     ota_bootstrap_task_handle));
    }

    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}
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
  ESP_LOGI("MAIN", "WiFi connected, starting MQTT init");
  esp_mqtt_app_start(NULL);
  mqtt_start_task_handle = NULL;
  vTaskDelete(NULL);
}

static void ota_bootstrap_task(void *pvParameters) {
  const TickType_t wait_step = pdMS_TO_TICKS(500);
  const TickType_t wait_timeout = pdMS_TO_TICKS(30000);
  TickType_t waited = 0;

  ESP_LOGI("MAIN", "Starting OTA bootstrap task");
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
    vTaskDelay(pdMS_TO_TICKS(5000));
  }

  while ((SysCB.SysEventFlag & CONNECT_WIFI) &&
         (SysCB.SysEventFlag & CONNECT_MQTT) &&
         !(SysCB.SysEventFlag & CONNECT_PING) && waited < 120000) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    waited += 3000;
  }

  if ((SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGI("MAIN", "Running OTA check...");
    WiFi_Cat1_CheckOTATask(0);
  }

  if (!boot_saved_wifi_pending || boot_saved_wifi_ap_started) {
    ESP_LOGI("MAIN", "OTA bootstrap completed");
  } else if (mqtt_start_task_handle != NULL) {
    ESP_LOGW("MAIN", "MQTT not ready after OTA bootstrap, will retry");
  }

  ota_bootstrap_task_handle = NULL;
  vTaskDelete(NULL);
}

static void start_boot_saved_wifi_ap_fallback(void) {
  if (boot_saved_wifi_pending) {
    boot_saved_wifi_ap_started = true;
    boot_saved_wifi_pending = false;
    ESP_LOGI("MAIN", "Boot saved WiFi failed, starting AP provision");
    wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");
  }
}

static void unified_sensor_upload_task(void *pvParameters) {
  (void)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(10000));

  while (1) {
    if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    iic_sensor_data_t iic_data;
    soil_sensor_data_t soil_data;
    bool have_iic = (iic_sensor_get_data(&iic_data) == ESP_OK);
    bool have_soil = (soil_sensor_read_data(&soil_data) == ESP_OK);

    if (have_iic && have_soil) {
      WiFi_Cat1_AllDataPost(
          iic_data.temperature, iic_data.humidity, iic_data.lux,
          soil_data.temperature, soil_data.humidity, soil_data.ec,
          soil_data.nitrogen, soil_data.phosphorus, soil_data.potassium,
          0.0f, 0.0f, 0.0f);
    } else if (have_iic) {
      WiFi_Cat1_GatewayDataPost(iic_data.temperature, iic_data.humidity,
                                iic_data.lux);
    } else if (have_soil) {
      WiFi_Cat1_SoilDataPost(soil_data.temperature, soil_data.humidity,
                             soil_data.ec, soil_data.nitrogen,
                             soil_data.phosphorus, soil_data.potassium);
    } else {
      ESP_LOGW("UPLOAD", "No sensor data available for upload");
    }

    if ((SysCB.SysEventFlag & SUB_NODE_DATA_READY)) {
      node_sensor_data_t *nd = &SysCB.last_node_data;
      WiFi_Cat1_NodeDataPost(nd->temperature, nd->humidity, nd->lightlux);
    }

    vTaskDelay(pdMS_TO_TICKS(60000));
  }
}

static void cat1_delayed_start_task(void *pvParameters) {
  (void)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(60000));
  start_Cat1Task(NULL);
  vTaskDelete(NULL);
}

static void lora_poll_task(void *pvParameters) {
  uint32_t last_test_send = 0;
  uint32_t last_query_send = 0;
  uint32_t lora_retry_count = 0;

  ESP_LOGI("LORA_TASK", "LoRa polling task started");
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
                 "LoRa sub-node not responding, retry query (%lu)",
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

    if (mqtt_start_task_handle == NULL && !mem_guard_level_at_least(MEM_LEVEL_CRITICAL)) {
      xTaskCreate(mqtt_start_task, "mqtt_start", 4096, NULL, 5,
                  &mqtt_start_task_handle);
    }
    if (ota_bootstrap_task_handle == NULL && !mem_guard_level_at_least(MEM_LEVEL_CRITICAL)) {
      xTaskCreate(ota_bootstrap_task, "ota_boot", 12288, NULL, 5,
                  &ota_bootstrap_task_handle);
    }
  } else if (state == WIFI_STATE_DISCONNECTED) {
    gateway_fw_reported = false;
    ESP_LOGW("MAIN", "WiFi disconnected");
    start_boot_saved_wifi_ap_fallback();
  }
}


/* LVGL port interfaces */
extern void lv_port_disp_init(void);
extern void lv_port_indev_init(void);
extern void lv_port_tick_init(void);


/* LVGL task - reads sensors and updates the dashboard UI */
static void lvgl_task(void *pvParameters) {
  (void)pvParameters;

  /* Give sensors time to initialise before first read */
  vTaskDelay(pdMS_TO_TICKS(3000));

  iic_sensor_data_t iic_data;
  soil_sensor_data_t soil_data;
  bool have_iic;
  bool have_soil;
  uint32_t tick = 0;

  while (1) {
    /* Degradation: pause LVGL refresh when memory is critically low */
    if (mem_guard_lvgl_paused()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    lv_timer_handler();

    /* Refresh sensor readings every ~1 s (every 60 LVGL ticks at 16 ms) */
    if ((tick % 60) == 0) {
      /* I2C environmental sensors (SHT30 + BH1750) */
      have_iic = (iic_sensor_get_data(&iic_data) == ESP_OK);
      /* Soil UART sensor */
      have_soil = (soil_sensor_read_data(&soil_data) == ESP_OK);

      lvgl_ui_update(
          have_iic  ? iic_data.temperature : -99.0f,
          have_iic  ? iic_data.humidity    : -99.0f,
          have_iic  ? iic_data.lux         : -99.0f,
          have_soil ? soil_data.temperature : -99.0f,
          have_soil ? soil_data.humidity    : -99.0f,
          have_soil ? soil_data.ec          : -99.0f,
          have_soil ? soil_data.ph          : -99.0f,
          have_soil ? soil_data.nitrogen    : -99.0f,
          have_soil ? soil_data.phosphorus  : -99.0f,
          have_soil ? soil_data.potassium   : -99.0f,
          have_soil ? soil_data.salinity    : -99.0f);

      /* System status */
      bool w_ok = (SysCB.SysEventFlag & CONNECT_WIFI) != 0;
      bool m_ok = (SysCB.SysEventFlag & CONNECT_MQTT) != 0;
      bool l_ok = (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) != 0;
      bool o_ok = (SysCB.SysEventFlag & OTA_RUNNING) != 0;

      lvgl_ui_update_sys(w_ok, m_ok, l_ok, o_ok, NULL, CURRENT_FW_VERSION);
    }

    tick++;
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}
void app_main(void) {
  app_configure_log_levels();
  ESP_LOGI("FIRMWARE", "Current firmware version: %s",
           WiFi_Cat1_GetRuntimeFirmwareVersion());

  ESP_ERROR_CHECK(EEprom_Init());
  EEprom_ReadInfo();

  srand((unsigned int)time(NULL));

#if (SOIL_UART_POWER_PIN >= 0) || (SOIL_UART_GND_PIN >= 0)
  gpio_config_t power_conf = {
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask =
          ((SOIL_UART_POWER_PIN >= 0) ? (1ULL << SOIL_UART_POWER_PIN) : 0) |
          ((SOIL_UART_GND_PIN >= 0)   ? (1ULL << SOIL_UART_GND_PIN)   : 0),
      .pull_down_en = 0,
      .pull_up_en = 0,
  };
  gpio_config(&power_conf);

  if (SOIL_UART_POWER_PIN >= 0) gpio_set_level(SOIL_UART_POWER_PIN, 1);
  if (SOIL_UART_GND_PIN >= 0)   gpio_set_level(SOIL_UART_GND_PIN, 0);
#endif
  vTaskDelay(pdMS_TO_TICKS(500));

  WiFi_Cat1_InitGPIO();
  CAT1_POWER(0);
  ESP_LOGI("MAIN", "System startup complete, WiFi priority, CAT1 off");

  bsp_led_init();
  wifi_manager_init();
  wifi_set_state_callback(wifi_state_callback);

  wifi_credentials_t saved_wifi = {0};
  ESP_LOGI("MAIN", "Loading saved WiFi config");
  if (wifi_manager_load_saved_config(&saved_wifi)) {
    ESP_LOGI("MAIN", "Using saved WiFi config: %s", saved_wifi.ssid);
    boot_saved_wifi_pending = true;
    boot_saved_wifi_ap_started = false;
    wifi_manager_connect(saved_wifi.ssid, saved_wifi.password);
  } else {
    boot_saved_wifi_pending = false;
    boot_saved_wifi_ap_started = true;
    ESP_LOGI("MAIN", "No saved WiFi config, starting AP provision");
    wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");
  }

  xTaskCreate(unified_sensor_upload_task, "sensor_upload", 8192, NULL, 5, NULL);
  xTaskCreate(cat1_delayed_start_task, "cat1_delay", 4096, NULL, 4, NULL);

  soil_sensor_init();

  /* LVGL init */
  ESP_LOGI("MAIN", "Initializing LVGL...");
  lv_init();
  lv_port_tick_init();
  lv_port_disp_init();
  lv_port_indev_init();

  lvgl_ui_create();
  ESP_LOGI("MAIN", "LVGL sensor dashboard started");

  xTaskCreate(lvgl_task, "lvgl", 20480, NULL, 3, NULL);

  /* Initialise memory guard after all subsystems are up */
  mem_guard_init();

  LoRa_Init();
  xTaskCreate(lora_poll_task, "lora_poll_task", 4096, NULL, 6, NULL);

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
