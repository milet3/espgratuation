/*
 * ota_manager.h -- OTA firmware update subsystem
 *
 * Extracted from wifi_cat1 for modularity.
 * Handles: OneNET FuseOTA, streaming HTTP OTA, ZC chunked OTA,
 *          OTA memory pool, version reporting, reboot notifications.
 */

#ifndef __OTA_MANAGER_H
#define __OTA_MANAGER_H

#include "app_config.h"
#include "bsp_uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── OTA chunk queue / pool ── */
#define OTA_ZC_QUEUE_LEN        8
#define OTA_ZC_SEND_TIMEOUT_MS  200
#define OTA_ZC_SEND_RETRY       3
#define OTA_RANGE_SIZE          256
#define OTA_ZC_WRITE_RETRY_MAX  2
#define OTA_ZC_VERIFY_WRITE     1

/* ── OTA chunk pool ── */
#define OTA_ZC_CHUNK_DATA_MAX   1024
#define OTA_ZC_POOL_SIZE        OTA_ZC_QUEUE_LEN

/* ── OTA chunk structs ── */
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
  uint8_t  ota_staflag;
  uint8_t  is_last;
  uint8_t  data[];
} OTA_ZC_Chunk;

/* ── OTA statistics ── */
typedef struct {
  uint32_t enqueued;
  uint32_t enqueue_blocked;
  uint32_t enqueue_fail;
  uint32_t processed;
  uint32_t write_retry;
  uint32_t write_fail;
} OTA_ZC_Stats;

/* ── OTA memory pool API ── */
void ota_zc_pool_init(void);
void ota_zc_pool_deinit(void);
OTA_ZC_Chunk *ota_zc_pool_acquire(uint16_t datalen);
void ota_zc_pool_release(OTA_ZC_Chunk *chunk);

/* ── OTA public API ── */
const char *WiFi_Cat1_GetRuntimeFirmwareVersion(void);
void WiFi_Cat1_ReportVersion(const char *id);
void WiFi_Cat1_PropertyVersion(uint8_t num);
void WiFi_Cat1_ReportBootOtaResult(void);

void WiFi_Cat1_StartOTA(const char *url, const char *token, uint8_t ota_staflag);
void WiFi_Cat1_OTADownload(uint16_t total_size, uint16_t ota_staflag, uint8_t reserved);
void OTAServer_process(uint8_t *data, uint16_t datalen, uint32_t page_index,
                       uint8_t ota_staflag, uint8_t is_last);

void OneNET_FuseOTA_CheckTask(void);
void WiFi_Cat1_CheckOTATask(uint8_t num);

/* ── OTA notify / reboot ── */
void WiFi_Cat1_RequestOtaNotifyReboot(void);
bool WiFi_Cat1_BeginPendingOtaNotifyBootstrap(void);
void WiFi_Cat1_FinishOtaNotifyBootstrap(void);
bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void);

/* ── Globals ── */
extern QueueHandle_t OTA_ZC_Queue;
extern OTA_ZC_Stats   g_ota_zc_stats;

#ifdef __cplusplus
}
#endif

#endif /* __OTA_MANAGER_H */
