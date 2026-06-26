#ifndef __WIFI_CAT1_H
#define __WIFI_CAT1_H

#include "app_config.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#define OTA_ZC_QUEUE_LEN 8
#define OTA_ZC_SEND_TIMEOUT_MS 200
#define OTA_ZC_SEND_RETRY 3
#define OTA_RANGE_SIZE 256
#define OTA_ZC_WRITE_RETRY_MAX 2
#define OTA_ZC_VERIFY_WRITE 1
#define CAT1_TYPE 3

#define PACK_MAX_SIZE 1024
#define OTA_ZC_CHUNK_DATA_MAX 1024
#define OTA_ZC_POOL_SIZE OTA_ZC_QUEUE_LEN
#define PACK_NUM 6

#define WIFI_RESET(x) gpio_set_level(WIFI_RESET_PIN, (x))

#define CAT1_POWER(x)                                                          \
  do {                                                                         \
    if (CAT1_POWER_STATE_PIN >= 0)                                             \
      gpio_set_level(CAT1_POWER_STATE_PIN, (x));                               \
  } while (0)

#define CAT1_POWER_STA                                                         \
  (CAT1_POWER_STA_PIN >= 0 ? gpio_get_level(CAT1_POWER_STA_PIN) : 0)

#define CAT1_NET_STA                                                           \
  (CAT1_NET_STA_PIN >= 0 ? gpio_get_level(CAT1_NET_STA_PIN) : 0)

typedef struct {
  uint16_t Data_Packsta;
  uint16_t Data_totle;
  uint16_t Data_lenth[PACK_NUM];
  uint16_t Data_num;
  uint16_t Data_Packlen[PACK_NUM];
  char Data_Packbuff[PACK_NUM][PACK_MAX_SIZE];
} Pack_CB;

#define PACK_CB_LEN sizeof(Pack_CB)

typedef struct {
  int tid;
  int ota_num;
  int ota_counter;
  int ota_last;
  uint16_t ota_staflag;
} OTA_CB;

typedef struct {
  uint16_t len;
  uint32_t page_index;
  uint8_t ota_staflag;
  uint8_t is_last;
  uint8_t data[];
} OTA_ZC_Chunk;

typedef struct {
  uint32_t enqueued;
  uint32_t enqueue_blocked;
  uint32_t enqueue_fail;
  uint32_t processed;
  uint32_t write_retry;
  uint32_t write_fail;
} OTA_ZC_Stats;

/* OTA chunk memory pool API */
void ota_zc_pool_init(void);
void ota_zc_pool_deinit(void);
OTA_ZC_Chunk *ota_zc_pool_acquire(uint16_t datalen);
void ota_zc_pool_release(OTA_ZC_Chunk *chunk);

void WiFi_Cat1_InitGPIO(void);
void WiFi_Reset(void);
void Cat1_Reset(void);
void WiFi_ProcessData(uint8_t *data, uint16_t len);
void Cat1_ProcessData(uint8_t *data, uint16_t len);
void MqttServer_ProcessData(uint8_t *data, uint16_t len);
void OTAServer_process(uint8_t *data, uint16_t datalen, uint32_t page_index,
                       uint8_t ota_staflag, uint8_t is_last);
void WiFi_Cat1_ActiveEvent(void);

esp_err_t WiFi_Cat1_SubOnline(char sub_num, char mode);
void WiFi_Cat1_GatewayDataPost(float temp, float hum, float lux);
void WiFi_Cat1_NodeDataPost(float temp, float hum, float lux);
void WiFi_Cat1_SoilDataPost(float temp, float humi, float ec, float n, float p,
                            float k);
void WiFi_Cat1_AdcDataPost(float adc1, float adc2, float adc3);
void WiFi_Cat1_AllDataPost(float air_temp, float air_hum, float air_lux,
                           float soil_temp, float soil_humi, float soil_ec,
                           float soil_n, float soil_p, float soil_k, float adc1,
                           float adc2, float adc3);
void WiFi_Cat1_SubPropertyGet(char sub_num);
void WiFi_Cat1_ReportVersion(const char *id);
const char *WiFi_Cat1_GetRuntimeFirmwareVersion(void);
void WiFi_Cat1_StartOTA(const char *url, const char *token,
                        uint8_t ota_staflag);
void OneNET_FuseOTA_CheckTask(void);
void WiFi_Cat1_PropertyVersion(uint8_t num);
void WiFi_Cat1_CheckOTATask(uint8_t num);
void WiFi_Cat1_OTADownload(uint16_t a, uint16_t b, uint8_t c);
void WiFi_Cat1_RequestOtaNotifyReboot(void);
bool WiFi_Cat1_BeginPendingOtaNotifyBootstrap(void);
void WiFi_Cat1_FinishOtaNotifyBootstrap(void);
bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void);

void start_Cat1Task(void *argument);
void Cat1_AT_Mqtt_Task(void *pvParameters);
esp_err_t Cat1_AT_MqttPublish(const char *topic, const char *payload);

extern QueueHandle_t OTA_ZC_Queue;
extern OTA_ZC_Stats g_ota_zc_stats;

#endif

