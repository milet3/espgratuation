#include "app_config.h"
#include "bsp_led.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_mqtt.h"
#include "IIC_SENSOR.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mw1268_app.h"
#include "soil_sensor.h"
#include "wifi_cat1.h"
#include "wifi_manager.h"
#include "lvgl.h"
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

static void app_configure_log_levels(void)
{
  esp_log_level_set("*", ESP_LOG_NONE);
  esp_log_level_set("OTA", ESP_LOG_INFO);
  esp_log_level_set("OTA_MQTT", ESP_LOG_INFO);
}

static void mqtt_start_task(void *pvParameters)
{
  (void)pvParameters;

  vTaskDelay(pdMS_TO_TICKS(1000));
  ESP_LOGI("MAIN", "Network ready, starting MQTT");
  esp_mqtt_app_start(NULL);
  mqtt_start_task_handle = NULL;
  vTaskDelete(NULL);
}

static void ota_bootstrap_task(void *pvParameters)
{
  (void)pvParameters;

  const TickType_t wait_step = pdMS_TO_TICKS(500);
  const TickType_t wait_timeout = pdMS_TO_TICKS(30000);
  TickType_t waited = 0;
  bool notify_triggered_bootstrap = WiFi_Cat1_BeginPendingOtaNotifyBootstrap();

  ESP_LOGI("OTA", "Boot OTA bootstrap task started");
  ESP_LOGI("OTA", "ota_boot stack watermark=%u",
           (unsigned int)uxTaskGetStackHighWaterMark(NULL));
  if (notify_triggered_bootstrap)
  {
    ESP_LOGI("OTA",
             "Detected pending ota/inform reboot request, continuing with unified boot OTA flow");
  }

  /* Wait for MQTT over any network (WiFi or CAT1 PPPoS) */
  while (!(SysCB.SysEventFlag & CONNECT_MQTT) && waited < wait_timeout)
  {
    vTaskDelay(wait_step);
    waited += wait_step;
  }

  if ((SysCB.SysEventFlag & CONNECT_MQTT) && !gateway_fw_reported)
  {
    WiFi_Cat1_PropertyVersion(0);
    gateway_fw_reported = true;
    vTaskDelay(pdMS_TO_TICKS(1500));
  }

  if (SysCB.SysEventFlag & CONNECT_MQTT)
  {
    WiFi_Cat1_ReportBootOtaResult();
  }

  if ((SysCB.SysEventFlag & (CONNECT_WIFI | CONNECT_CAT1)) &&
      !(SysCB.SysEventFlag & OTA_RUNNING))
  {
    OneNET_FuseOTA_CheckTask();
  }

  if (notify_triggered_bootstrap)
  {
    WiFi_Cat1_FinishOtaNotifyBootstrap();
  }

  ota_bootstrap_task_handle = NULL;
  vTaskDelete(NULL);
}

static void start_boot_saved_wifi_ap_fallback(void)
{
  if (!boot_saved_wifi_pending || boot_saved_wifi_ap_started)
  {
    return;
  }

  boot_saved_wifi_pending = false;
  boot_saved_wifi_ap_started = true;

  ESP_LOGW("MAIN",
           "Saved WiFi connection failed, switching to AP provisioning mode");
  wifi_manager_cancel_connect_retry();
  esp_err_t err =
      wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");
  if (err != ESP_OK)
  {
    ESP_LOGE("MAIN", "Failed to start AP provisioning: %s",
             esp_err_to_name(err));
  }
}

void cat1_delayed_start_task(void *pvParameters)
{
  (void)pvParameters;

  ESP_LOGI("MAIN", "CAT1 fallback link monitor task started");
  vTaskDelay(pdMS_TO_TICKS(120000));

  if (SysCB.SysEventFlag & CONNECT_WIFI)
  {
    ESP_LOGI("MAIN", "WiFi is connected, keeping CAT1 powered off");
  }
  else
  {
    ESP_LOGW("MAIN", "WiFi unavailable, enabling CAT1 PPPoS fallback link");
    Cat1_Reset();
    if (Cat1_PPPoS_Init() == ESP_OK)
    {
      ESP_LOGI("MAIN", "CAT1 PPPoS is up, starting MQTT and OTA bootstrap");
      if (mqtt_start_task_handle == NULL)
      {
        xTaskCreate(mqtt_start_task, "mqtt_start", 4096, NULL, 5,
                    &mqtt_start_task_handle);
      }
      if (ota_bootstrap_task_handle == NULL)
      {
        xTaskCreate(ota_bootstrap_task, "ota_boot", 12288, NULL, 3,
                    &ota_bootstrap_task_handle);
      }
    }
    else
    {
      ESP_LOGE("MAIN", "CAT1 PPPoS init failed");
    }
  }

  vTaskDelete(NULL);
}

static void unified_sensor_upload_task(void *pvParameters)
{
  (void)pvParameters;

  iic_sensor_data_t air_data = {0};
  soil_sensor_data_t soil_data = {0};

  vTaskDelay(pdMS_TO_TICKS(10000));

  while (1)
  {
    if (SysCB.SysEventFlag & OTA_RUNNING)
    {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    if (SysCB.SysEventFlag & CONNECT_MQTT)
    {
      if (!gateway_fw_reported)
      {
        WiFi_Cat1_PropertyVersion(0);
        gateway_fw_reported = true;
        vTaskDelay(pdMS_TO_TICKS(1000));
      }

      ESP_LOGI("UPLOAD", "Preparing gateway air data upload");
      if (iic_sensor_get_data(&air_data) == ESP_OK)
      {
        if (air_data.lux >= 0.0f)
        {
          ESP_LOGI("UPLOAD", "Air data upload T=%.2fC H=%.2f%% Lux=%.2f",
                   air_data.temperature, air_data.humidity, air_data.lux);
        }
        else
        {
          ESP_LOGW("UPLOAD",
                   "Light sensor not ready, uploading only temperature and humidity");
          ESP_LOGI("UPLOAD", "Air data upload T=%.2fC H=%.2f%%",
                   air_data.temperature, air_data.humidity);
        }
        WiFi_Cat1_GatewayDataPost(air_data.temperature, air_data.humidity,
                                  air_data.lux);
      }
      else
      {
        ESP_LOGW("UPLOAD", "Air sensor data not ready, skipping this upload");
      }
      vTaskDelay(pdMS_TO_TICKS(2000));

      if (soil_sensor_read_data(&soil_data) == ESP_OK)
      {
        ESP_LOGI("UPLOAD",
                 "Soil data upload T=%.2fC H=%.2f%% EC=%.2f N=%.1f P=%.1f K=%.1f",
                 soil_data.temperature, soil_data.humidity, soil_data.ec,
                 soil_data.nitrogen, soil_data.phosphorus, soil_data.potassium);
        WiFi_Cat1_SoilDataPost(soil_data.temperature, soil_data.humidity,
                               soil_data.ec, soil_data.nitrogen,
                               soil_data.phosphorus, soil_data.potassium);
      }
      else
      {
        ESP_LOGW("UPLOAD", "Soil sensor data not ready, skipping this upload");
      }
      vTaskDelay(pdMS_TO_TICKS(2000));

      if (SysCB.SysEventFlag & SUB_LORA_CONFIRMED)
      {
        node_sensor_data_t *cached = &SysCB.last_node_data;
        if (cached->temperature != 0.0f || cached->humidity != 0.0f ||
            cached->lightlux != 0.0f)
        {
          ESP_LOGI("UPLOAD", "Node data upload T=%.2fC H=%.2f%% Lux=%.2f",
                   cached->temperature, cached->humidity, cached->lightlux);
          WiFi_Cat1_NodeDataPost(cached->temperature, cached->humidity,
                                 cached->lightlux);
        }
        else
        {
          ESP_LOGW("UPLOAD",
                   "Node link confirmed but cached data is all zero, skipping");
        }
      }
      else if (SysCB.SysEventFlag & SUB_LORA_CONFIRMED)
      {
        ESP_LOGW("UPLOAD",
                 "Node link is not confirmed, skipping cached node upload");
      }
      else
      {
        ESP_LOGW("UPLOAD",
                 "No valid node sensor report cached yet, skipping node upload");
      }
    }
    else
    {
      gateway_fw_reported = false;
    }

    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

static void lora_poll_task(void *pvParameters)
{
  (void)pvParameters;

  uint32_t last_query_send = 0;
  uint32_t lora_retry_count = 0;

  ESP_LOGI("LORA_TASK", "LoRa polling task started");
  vTaskDelay(pdMS_TO_TICKS(2000));
  LoRa_QueryNodeOnline();
  last_query_send = xTaskGetTickCount();

  while (1)
  {
    LoRa_ActiveEvent();

    if (SysCB.SysEventFlag & SUB_LORA_CONFIRMED)
    {
      lora_retry_count = 0;
    }
    else if (xTaskGetTickCount() - last_query_send > pdMS_TO_TICKS(5000))
    {
      ++lora_retry_count;
      if (lora_retry_count == 1 || (lora_retry_count % 6U) == 0U)
      {
        ESP_LOGW("LORA_TASK",
                 "LoRa node has not responded yet, retrying online query (%lu)",
                 (unsigned long)lora_retry_count);
      }
      LoRa_QueryNodeOnline();
      last_query_send = xTaskGetTickCount();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}


/* ── LVGL port interfaces ── */
extern void lv_port_disp_init(void);
extern void lv_port_indev_init(void);
extern void lv_port_tick_init(void);

/* ── LVGL task ── */
static void lvgl_task(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ── Simple LVGL demo ── */
static void lvgl_create_demo(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello LVGL v9.5!");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Click Me");
    lv_obj_center(btn_label);

    lv_obj_t *arc = lv_arc_create(scr);
    lv_obj_set_size(arc, 150, 150);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 80);
    lv_arc_set_value(arc, 60);
}

void wifi_state_callback(wifi_state_t state)
{
  if (state == WIFI_STATE_CONNECTED)
  {
    boot_saved_wifi_pending = false;
    gateway_fw_reported = false;

    if (mqtt_start_task_handle == NULL)
    {
      xTaskCreate(mqtt_start_task, "mqtt_start", 4096, NULL, 5,
                  &mqtt_start_task_handle);
    }
    if (ota_bootstrap_task_handle == NULL)
    {
      xTaskCreate(ota_bootstrap_task, "ota_boot", 12288, NULL, 3,
                  &ota_bootstrap_task_handle);
    }
  }
  else if (state == WIFI_STATE_DISCONNECTED)
  {
    gateway_fw_reported = false;
    ESP_LOGW("MAIN", "WiFi disconnected");
    start_boot_saved_wifi_ap_fallback();
  }
}

void app_main(void)
{
  app_configure_log_levels();
  ESP_LOGI("OTA", "Current firmware version: %s",
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
  ESP_LOGI("MAIN", "System booted: prefer WiFi and keep CAT1 powered off");

  ESP_ERROR_CHECK(bsp_led_init());
  wifi_manager_init();
  wifi_set_state_callback(wifi_state_callback);

  wifi_credentials_t saved_wifi = {0};
  ESP_LOGI("MAIN", "Loading saved WiFi configuration");
  if (wifi_manager_load_saved_config(&saved_wifi))
  {
    ESP_LOGI("MAIN", "Connecting with saved WiFi: %s", saved_wifi.ssid);
    boot_saved_wifi_pending = true;
    boot_saved_wifi_ap_started = false;
    wifi_manager_connect(saved_wifi.ssid, saved_wifi.password);
  }
  else
  {
    boot_saved_wifi_pending = false;
    boot_saved_wifi_ap_started = true;
    ESP_LOGI("MAIN", "No saved WiFi found, starting AP provisioning");
    wifi_manager_start_ap_provisioning("ESP32_Config", "12345678");
  }

  esp_err_t air_sensor_err = iic_sensor_task_start();
  if (air_sensor_err != ESP_OK)
  {
    ESP_LOGE("MAIN", "Failed to start air sensor task: %s",
             esp_err_to_name(air_sensor_err));
  }

  xTaskCreate(unified_sensor_upload_task, "sensor_upload", 8192, NULL, 5, NULL);
  xTaskCreate(cat1_delayed_start_task, "cat1_delay", 4096, NULL, 5, NULL);

  soil_sensor_init();
  /* LoRa disabled - pins conflict with TFT SPI (GPIO 15-18) */
  ESP_LOGW("MAIN", "LoRa disabled (GPIO conflict with TFT SPI)");

  /* ── LVGL init ── */
  ESP_LOGI("MAIN", "Initializing LVGL...");
  lv_init();
  lv_port_tick_init();
  lv_port_disp_init();
  lv_port_indev_init();

  lvgl_create_demo();
  ESP_LOGI("MAIN", "LVGL demo started");

  xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);

  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
