#include "ota_manager.h"
#include "mem_guard.h"
#include "app_config.h"
#include "bsp_storage.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ctype.h>
#include "math.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "utils_hmac.h"
#include "utils_md5.h"
#include "esp_mqtt.h"

static const char *TAG = "OTA";

#define OTA_SOUTH_TYPE 3

#ifndef OTA_NUMERIC_PID
#define OTA_NUMERIC_PID ""
#endif
#ifndef OTA_DEVICE_AUTHINFO
#define OTA_DEVICE_AUTHINFO ""
#endif
#ifndef OTA_DEVICE_ID
#define OTA_DEVICE_ID ""
#endif

extern esp_err_t Cat1_AT_MqttPublish(const char *topic, const char *payload);

/* =================================================================
 * OTA ZC pre-allocated chunk pool --- zero heap fragmentation
 * ================================================================= */
#define OTA_ZC_CHUNK_SIZE (sizeof(OTA_ZC_Chunk) + OTA_ZC_CHUNK_DATA_MAX)

typedef struct
{
  OTA_ZC_Chunk *chunks[OTA_ZC_POOL_SIZE];
  int free_stack[OTA_ZC_POOL_SIZE];
  int free_count;
  SemaphoreHandle_t mutex;
  bool initialized;
} OTA_ZC_Pool;

static OTA_ZC_Pool g_ota_zc_pool = {.initialized = false};

void ota_zc_pool_init(void)
{
  if (g_ota_zc_pool.initialized)
    return;
  g_ota_zc_pool.mutex = xSemaphoreCreateMutex();
  if (g_ota_zc_pool.mutex == NULL)
  {
    ESP_LOGE(TAG, "ota_zc_pool: mutex fail");
    return;
  }
  int allocated = 0;
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++)
  {
    g_ota_zc_pool.chunks[i] = (OTA_ZC_Chunk *)heap_caps_malloc(
        OTA_ZC_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    if (g_ota_zc_pool.chunks[i] == NULL)
      g_ota_zc_pool.chunks[i] = (OTA_ZC_Chunk *)heap_caps_malloc(
          OTA_ZC_CHUNK_SIZE, MALLOC_CAP_INTERNAL);
    if (g_ota_zc_pool.chunks[i] == NULL)
      break;
    allocated++;
  }
  if (allocated == 0)
  {
    ESP_LOGE(TAG, "ota_zc_pool: no mem");
    vSemaphoreDelete(g_ota_zc_pool.mutex);
    g_ota_zc_pool.mutex = NULL;
    return;
  }
  for (int i = 0; i < allocated; i++)
    g_ota_zc_pool.free_stack[i] = i;
  g_ota_zc_pool.free_count = allocated;
  g_ota_zc_pool.initialized = true;
  ESP_LOGI(TAG, "ota_zc_pool init: %d chunks x %u bytes",
           allocated, (unsigned int)OTA_ZC_CHUNK_SIZE);
}

void ota_zc_pool_deinit(void)
{
  if (!g_ota_zc_pool.initialized)
    return;
  if (g_ota_zc_pool.mutex)
    xSemaphoreTake(g_ota_zc_pool.mutex, portMAX_DELAY);
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++)
  {
    if (g_ota_zc_pool.chunks[i])
    {
      free(g_ota_zc_pool.chunks[i]);
      g_ota_zc_pool.chunks[i] = NULL;
    }
  }
  g_ota_zc_pool.free_count = 0;
  g_ota_zc_pool.initialized = false;
  if (g_ota_zc_pool.mutex)
  {
    xSemaphoreGive(g_ota_zc_pool.mutex);
    vSemaphoreDelete(g_ota_zc_pool.mutex);
    g_ota_zc_pool.mutex = NULL;
  }
  ESP_LOGI(TAG, "ota_zc_pool deinit");
}

OTA_ZC_Chunk *ota_zc_pool_acquire(uint16_t datalen)
{
  if (!g_ota_zc_pool.initialized || datalen > OTA_ZC_CHUNK_DATA_MAX)
    return NULL;
  OTA_ZC_Chunk *chunk = NULL;
  if (g_ota_zc_pool.mutex)
    xSemaphoreTake(g_ota_zc_pool.mutex, pdMS_TO_TICKS(500));
  if (g_ota_zc_pool.free_count > 0)
  {
    int idx = g_ota_zc_pool.free_stack[--g_ota_zc_pool.free_count];
    chunk = g_ota_zc_pool.chunks[idx];
    chunk->len = datalen;
  }
  if (g_ota_zc_pool.mutex)
    xSemaphoreGive(g_ota_zc_pool.mutex);
  return chunk;
}

void ota_zc_pool_release(OTA_ZC_Chunk *chunk)
{
if (!g_ota_zc_pool.initialized || chunk == NULL)
    return;
  if (g_ota_zc_pool.mutex)
    xSemaphoreTake(g_ota_zc_pool.mutex, pdMS_TO_TICKS(500));
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++)
  {
    if (g_ota_zc_pool.chunks[i] == chunk)
    {
      if (g_ota_zc_pool.free_count < OTA_ZC_POOL_SIZE)
        g_ota_zc_pool.free_stack[g_ota_zc_pool.free_count++] = i;
      break;
    }
  }
  if (g_ota_zc_pool.mutex)
    xSemaphoreGive(g_ota_zc_pool.mutex);
}

/* =================================================================
 * ZC OTA writer task --- dequeues chunks, writes to flash, releases to pool
 * ================================================================= */
static TaskHandle_t g_ota_zc_task_handle = NULL;
static esp_ota_handle_t g_ota_zc_ota_handle = 0;
static bool g_ota_zc_active = false;

static void ota_zc_writer_task(void *pvParameters)
{
  (void)pvParameters;
  OTA_ZC_Chunk *chunk;

  while (g_ota_zc_active)
  {
    if (xQueueReceive(OTA_ZC_Queue, &chunk, pdMS_TO_TICKS(5000)) == pdTRUE)
    {
      if (chunk != NULL)
      {
        esp_err_t err = esp_ota_write(g_ota_zc_ota_handle, chunk->data, chunk->len);
        if (err != ESP_OK)
        {
          ESP_LOGE(TAG, "ZC OTA write error: %s (0x%x)", esp_err_to_name(err), err);
          g_ota_zc_active = false;
        }
        ota_zc_pool_release(chunk);
      }
    }
  }

  /* Drain remaining queued chunks */
  while (xQueueReceive(OTA_ZC_Queue, &chunk, 0) == pdTRUE)
  {
    if (chunk != NULL)
    {
      ota_zc_pool_release(chunk);
    }
  }

  g_ota_zc_task_handle = NULL;
  vTaskDelete(NULL);
}

esp_err_t ota_zc_subsystem_start(void)
{
  if (g_ota_zc_active)
    return ESP_OK;

  ota_zc_pool_init();

  if (OTA_ZC_Queue == NULL)
  {
    OTA_ZC_Queue = xQueueCreate(OTA_ZC_QUEUE_LEN, sizeof(OTA_ZC_Chunk *));
    if (OTA_ZC_Queue == NULL)
    {
      ESP_LOGE(TAG, "ZC OTA: queue create failed");
      ota_zc_pool_deinit();
      return ESP_ERR_NO_MEM;
    }
  }

  g_ota_zc_active = true;
  BaseType_t ret = xTaskCreate(ota_zc_writer_task, "ota_zc_writer",
                               4096, NULL, 5, &g_ota_zc_task_handle);
  if (ret != pdPASS)
  {
    ESP_LOGE(TAG, "ZC OTA: writer task create failed");
    g_ota_zc_active = false;
    ota_zc_pool_deinit();
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "ZC OTA subsystem started (pool + queue + writer)");
  return ESP_OK;
}

void ota_zc_subsystem_stop(void)
{
  g_ota_zc_active = false;

  if (g_ota_zc_task_handle != NULL)
  {
    while (eTaskGetState(g_ota_zc_task_handle) != eDeleted)
      vTaskDelay(pdMS_TO_TICKS(100));
    g_ota_zc_task_handle = NULL;
  }

  if (OTA_ZC_Queue != NULL)
  {
    vQueueDelete(OTA_ZC_Queue);
    OTA_ZC_Queue = NULL;
  }

  ota_zc_pool_deinit();
  ESP_LOGI(TAG, "ZC OTA subsystem stopped");
}



QueueHandle_t OTA_ZC_Queue = NULL;
static char g_ota_download_auth[512] = {0};
static char g_ota_status_url[512] = {0};
static char g_ota_target_version[64] = {0};
static int g_ota_expected_size = 0;
static char g_ota_expected_md5[64] = {0};
static const char *const g_ota_device_lookup_url_templates[] = {
    "https://api.heclouds.com/mqtt/v1/devices/%s",
    "http://api.heclouds.com/mqtt/v1/devices/%s",
    "http://api.onenet.hk.chinamobile.com/mqtt/v1/devices/%s",
};
static const char *const g_fuse_ota_check_url_templates[] = {
    "https://iot-api.heclouds.com/fuse-ota/%s/%s/check?type=%d&version=%s",
    "http://iot-api.heclouds.com/fuse-ota/%s/%s/check?type=%d&version=%s",
};
static const char *g_fuse_ota_download_url_template =
    "https://iot-api.heclouds.com/fuse-ota/%s/%s/%s/download";
static const char *g_fuse_ota_status_url_template =
    "https://iot-api.heclouds.com/fuse-ota/%s/%s/%s/status";
__attribute__((unused)) static const char *const g_ota_devinfo_url_templates[] = {
    "http://ota.heclouds.com/ota/devInfo",
    "http://iot-api.heclouds.com/ota/devInfo",
};
static const char *const g_ota_legacy_check_url_templates[] __attribute__((unused)) = {
    "http://ota.heclouds.com/ota/south/check?product_id=%s&device_name=%s&version=%s",
};
static const char *const g_ota_check_url_templates[] __attribute__((unused)) = {
    "http://ota.heclouds.com/ota/south/check?dev_id=%s&manuf=%s&model=%s&type=%d&version=%s&cdn=true",
    "http://iot-api.heclouds.com/ota/south/check?dev_id=%s&manuf=%s&model=%s&type=%d&version=%s&cdn=true",
};

#ifndef OTA_PRODUCT_ACCESSKEY
#define OTA_PRODUCT_ACCESSKEY Accesskey
#endif

#define OTA_PENDING_INFO_MAGIC 0x4F544132UL
#define OTA_PENDING_INFO_KEY "ota_pending"

typedef struct
{
  uint32_t magic;
  char status_url[512];
  char target_version[64];
} ota_pending_info_t;

const char *WiFi_Cat1_GetRuntimeFirmwareVersion(void)
{
  const esp_app_desc_t *app_desc = esp_app_get_description();
  if (app_desc != NULL && app_desc->version[0] != '\0')
  {
    return app_desc->version;
  }
  return CURRENT_FW_VERSION;
}

__attribute__((unused)) static const char *gateway_firmware_version(void)
{
  return WiFi_Cat1_GetRuntimeFirmwareVersion();
}

__attribute__((unused)) static bool ota_url_is_https(const char *url)
{
  return url != NULL && strncmp(url, "https://", strlen("https://")) == 0;
}

__attribute__((unused)) static bool looks_like_hex_string(const char *value)
{
  if (value == NULL)
  {
    return false;
  }

  size_t len = strlen(value);
  if (len < 16 || (len % 2) != 0)
  {
    return false;
  }

  for (size_t i = 0; i < len; ++i)
  {
    if (!isxdigit((unsigned char)value[i]))
    {
      return false;
    }
  }
  return true;
}

__attribute__((unused)) static bool looks_like_decimal_string(const char *value)
{
  if (value == NULL || value[0] == '\0')
  {
    return false;
  }

  for (size_t i = 0; value[i] != '\0'; ++i)
  {
    if (!isdigit((unsigned char)value[i]))
    {
      return false;
    }
  }
  return true;
}

static bool copy_json_string_or_number(cJSON *item, char *out, size_t out_len)
{
  if (item == NULL || out == NULL || out_len == 0)
  {
    return false;
  }

  if (cJSON_IsString(item) && item->valuestring != NULL &&
      item->valuestring[0] != '\0')
  {
    int ret = snprintf(out, out_len, "%s", item->valuestring);
    return ret > 0 && ret < (int)out_len;
  }

  if (cJSON_IsNumber(item))
  {
    int ret = snprintf(out, out_len, "%.0f", item->valuedouble);
    return ret > 0 && ret < (int)out_len;
  }

  return false;
}

__attribute__((unused)) static bool looks_like_base64_secret(const char *value)
{
  if (value == NULL || value[0] == '\0')
  {
    return false;
  }

  size_t len = strlen(value);
  if (len < 8 || (len % 4) == 1)
  {
    return false;
  }

  for (size_t i = 0; i < len; ++i)
  {
    unsigned char c = (unsigned char)value[i];
    if (isalnum(c) || c == '+' || c == '/' || c == '=')
    {
      continue;
    }
    return false;
  }

  return true;
}

__attribute__((unused)) static const char *json_string_or_fallback(cJSON *primary, cJSON *fallback,
                                                                   const char *default_value)
{
  if (cJSON_IsString(primary) && primary->valuestring != NULL &&
      primary->valuestring[0] != '\0')
  {
    return primary->valuestring;
  }
  if (cJSON_IsString(fallback) && fallback->valuestring != NULL &&
      fallback->valuestring[0] != '\0')
  {
    return fallback->valuestring;
  }
  return default_value;
}

typedef enum
{
  OTA_CHECK_PARSE_INCONCLUSIVE,
  OTA_CHECK_PARSE_NO_TASK,
  OTA_CHECK_PARSE_HAS_TASK,
  OTA_CHECK_PARSE_SERVER_ERROR,
} ota_check_parse_result_t;

static ota_check_parse_result_t ota_parse_check_response(
    const char *body, char *download_url, size_t download_url_len,
    char *download_token, size_t download_token_len)
{
  if (body == NULL || download_url == NULL || download_url_len == 0 ||
      download_token == NULL || download_token_len == 0)
  {
    return OTA_CHECK_PARSE_INCONCLUSIVE;
  }

  download_url[0] = '\0';
  download_token[0] = '\0';

  cJSON *root = cJSON_Parse(body);
  if (root == NULL)
  {
    ESP_LOGE(TAG, "Failed to parse OTA check response");
    return OTA_CHECK_PARSE_INCONCLUSIVE;
  }

  cJSON *errno_item = cJSON_GetObjectItem(root, "errno");
  cJSON *error_item = cJSON_GetObjectItem(root, "error");
  if (cJSON_IsNumber(errno_item))
  {
    if (errno_item->valueint == 11)
    {
      ESP_LOGI(TAG, "No executable OTA task for this firmware version");
      cJSON_Delete(root);
      return OTA_CHECK_PARSE_NO_TASK;
    }
    if (errno_item->valueint != 0)
    {
      ESP_LOGW(TAG, "OTA check returned errno=%d, error=%s",
               errno_item->valueint,
               cJSON_IsString(error_item) ? error_item->valuestring : "unknown");
      if (errno_item->valueint == 1)
      {
        ESP_LOGW(TAG,
                 "OTA auth failed. Check OTA_PRODUCT_ACCESSKEY and confirm it "
                 "comes from OneNET OTA product details");
      }
      else if (errno_item->valueint == 26)
      {
        ESP_LOGW(TAG,
                 "OTA south/check rejected dev_id. Use the official "
                 "/ota/devInfo result or configure OTA_DEVICE_ID from the OTA "
                 "console; MQTT device_id is usually not accepted here.");
      }
      cJSON_Delete(root);
      return OTA_CHECK_PARSE_SERVER_ERROR;
    }
  }

  cJSON *code = cJSON_GetObjectItem(root, "code");
  if (cJSON_IsNumber(code) && code->valueint != 0 && code->valueint != 200)
  {
    cJSON *msg = cJSON_GetObjectItem(root, "msg");
    ESP_LOGW(TAG, "OTA check returned code=%d, msg=%s", code->valueint,
             cJSON_IsString(msg) ? msg->valuestring : "unknown");
    cJSON_Delete(root);
    return OTA_CHECK_PARSE_SERVER_ERROR;
  }

  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsObject(data))
  {
    ESP_LOGW(TAG, "OTA check response succeeded but did not include task data");
    cJSON_Delete(root);
    return OTA_CHECK_PARSE_SERVER_ERROR;
  }

  cJSON *url_item = cJSON_GetObjectItem(data, "url");
  if (cJSON_IsString(url_item) && url_item->valuestring[0] != '\0')
  {
    snprintf(download_url, download_url_len, "%s", url_item->valuestring);
  }
  else
  {
    cJSON *token_item = cJSON_GetObjectItem(data, "token");
    cJSON *ip_port_item = cJSON_GetObjectItem(data, "ipPort");
    if (cJSON_IsString(token_item) && cJSON_IsString(ip_port_item))
    {
      snprintf(download_token, download_token_len, "%s", token_item->valuestring);
      snprintf(download_url, download_url_len, "http://%s/ota/south/download/%s",
               ip_port_item->valuestring, token_item->valuestring);
    }
  }

  cJSON_Delete(root);

  if (download_url[0] == '\0')
  {
    ESP_LOGW(TAG, "OTA check succeeded but no download URL was returned");
    return OTA_CHECK_PARSE_SERVER_ERROR;
  }

  return OTA_CHECK_PARSE_HAS_TASK;
}

static void log_dns_server(const char *label,
                           const esp_netif_dns_info_t *dns_info)
{
  if (dns_info == NULL)
  {
    return;
  }

  if (dns_info->ip.type == ESP_IPADDR_TYPE_V4)
  {
    ESP_LOGI(TAG, "%s " IPSTR, label, IP2STR(&dns_info->ip.u_addr.ip4));
    return;
  }

  ESP_LOGI(TAG, "%s type=%d", label, dns_info->ip.type);
}

static void log_ota_network_snapshot(const char *phase)
{
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta == NULL)
  {
    ESP_LOGW(TAG, "[%s] WiFi STA netif is not available", phase);
    return;
  }

  esp_netif_t *default_netif = esp_netif_get_default_netif();
  if (default_netif != sta)
  {
    ESP_LOGW(TAG, "[%s] default netif is not WiFi STA", phase);
  }

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK)
  {
    ESP_LOGI(TAG, "[%s] STA IP=" IPSTR " GW=" IPSTR " MASK=" IPSTR, phase,
             IP2STR(&ip_info.ip), IP2STR(&ip_info.gw), IP2STR(&ip_info.netmask));
  }
  else
  {
    ESP_LOGW(TAG, "[%s] Failed to read STA IP info", phase);
  }

  esp_netif_dns_info_t dns_info;
  if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK)
  {
    log_dns_server("[OTA] DNS main:", &dns_info);
  }
  if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK)
  {
    log_dns_server("[OTA] DNS backup:", &dns_info);
  }
  if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_FALLBACK, &dns_info) == ESP_OK)
  {
    log_dns_server("[OTA] DNS fallback:", &dns_info);
  }
}

static int decode_secret_or_raw(const char *secret, unsigned char *key_buf,
                                size_t key_buf_len)
{
  if (secret == NULL || key_buf == NULL || key_buf_len == 0)
  {
    return 0;
  }

  memset(key_buf, 0, key_buf_len);
  if (looks_like_base64_secret(secret))
  {
    size_t secret_len = strlen(secret);
    size_t padded_len = secret_len;
    while ((padded_len % 4) != 0)
    {
      ++padded_len;
    }

    if (padded_len < 192)
    {
      char normalized_secret[192] = {0};
      memcpy(normalized_secret, secret, secret_len);
      for (size_t i = secret_len; i < padded_len; ++i)
      {
        normalized_secret[i] = '=';
      }

      int key_len = base64_decode(normalized_secret, key_buf);
      if (key_len > 0 && (size_t)key_len <= key_buf_len)
      {
        if (padded_len != secret_len)
        {
          ESP_LOGI(TAG, "Normalized base64 padding for OneNET secret");
        }
        return key_len;
      }
    }
  }

  size_t raw_len = strnlen(secret, key_buf_len);
  memcpy(key_buf, secret, raw_len);
  ESP_LOGW(TAG, "Secret is not valid base64, using raw bytes fallback");
  return (int)raw_len;
}

__attribute__((unused)) static esp_err_t build_onenet_token(const char *resource, const char *secret,
                                                            char *out, size_t out_len)
{
  if (resource == NULL || secret == NULL || out == NULL || out_len == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  unsigned char key_buf[128];
  int key_len = decode_secret_or_raw(secret, key_buf, sizeof(key_buf));
  if (key_len <= 0)
  {
    return ESP_FAIL;
  }

  char string_for_signature[256];
  int ret = snprintf(string_for_signature, sizeof(string_for_signature),
                     "%s\nsha1\n%s\n2018-10-31", UNIX, resource);
  if (ret < 0 || ret >= (int)sizeof(string_for_signature))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  char sign_bin[32] = {0};
  char sign_b64[128] = {0};
  char sign_url[192] = {0};
  char res_copy[128] = {0};
  char res_url[192] = {0};

  utils_hmac_sha1_hex(string_for_signature, strlen(string_for_signature),
                      sign_bin, (const char *)key_buf, key_len);
  base64_encode((const unsigned char *)sign_bin, sign_b64, 20);

  snprintf(res_copy, sizeof(res_copy), "%s", resource);
  URL_encode(sign_b64, strlen(sign_b64), sign_url);
  URL_encode(res_copy, strlen(res_copy), res_url);

  ret = snprintf(out, out_len,
                 "version=2018-10-31&res=%s&et=%s&method=sha1&sign=%s",
                 res_url, UNIX, sign_url);
  if (ret < 0 || ret >= (int)out_len)
  {
    return ESP_ERR_INVALID_SIZE;
  }

  return ESP_OK;
}

static esp_err_t read_http_body(esp_http_client_handle_t client, char **out_body,
                                size_t *out_len)
{
  if (client == NULL || out_body == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  size_t capacity = 1024;
  size_t length = 0;
  char *body = malloc(capacity);
  if (body == NULL)
  {
    return ESP_ERR_NO_MEM;
  }

  while (1)
  {
    if (length + 512 + 1 > capacity)
    {
      capacity *= 2;
      char *new_body = realloc(body, capacity);
      if (new_body == NULL)
      {
        free(body);
        return ESP_ERR_NO_MEM;
      }
      body = new_body;
    }

    int read_len =
        esp_http_client_read(client, body + length, capacity - length - 1);
    if (read_len < 0)
    {
      free(body);
      return ESP_FAIL;
    }
    if (read_len == 0)
    {
      break;
    }
    length += (size_t)read_len;
  }

  body[length] = '\0';
  *out_body = body;
  if (out_len != NULL)
  {
    *out_len = length;
  }
  return ESP_OK;
}

static esp_err_t ota_http_get(const char *url, const char *authorization,
                              int timeout_ms, int *status_code, char **out_body,
                              size_t *out_len)
{
  if (url == NULL || status_code == NULL || out_body == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  *status_code = 0;
  *out_body = NULL;
  if (out_len != NULL)
  {
    *out_len = 0;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = timeout_ms,
      .keep_alive_enable = false,
      .addr_type = HTTP_ADDR_TYPE_INET,
  };
  if (ota_url_is_https(url))
  {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL)
  {
    return ESP_ERR_NO_MEM;
  }

  if (authorization != NULL && authorization[0] != '\0')
  {
    esp_http_client_set_header(client, "Authorization", authorization);
  }
  esp_http_client_set_header(client, "Accept", "application/json");

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK)
  {
    esp_http_client_cleanup(client);
    return err;
  }

  esp_http_client_fetch_headers(client);
  *status_code = esp_http_client_get_status_code(client);
  err = read_http_body(client, out_body, out_len);
  esp_http_client_cleanup(client);
  return err;
}

static esp_err_t ota_http_request(const char *url, esp_http_client_method_t method,
                                  const char *authorization,
                                  const char *content_type, const char *body,
                                  int timeout_ms, int *status_code,
                                  char **out_body, size_t *out_len)
{
  if (url == NULL || status_code == NULL || out_body == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  *status_code = 0;
  *out_body = NULL;
  if (out_len != NULL)
  {
    *out_len = 0;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = method,
      .timeout_ms = timeout_ms,
      .keep_alive_enable = false,
      .addr_type = HTTP_ADDR_TYPE_INET,
  };
  if (ota_url_is_https(url))
  {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL)
  {
    return ESP_ERR_NO_MEM;
  }

  if (authorization != NULL && authorization[0] != '\0')
  {
    esp_http_client_set_header(client, "Authorization", authorization);
  }
  esp_http_client_set_header(client, "Accept", "application/json");
  if (content_type != NULL && content_type[0] != '\0')
  {
    esp_http_client_set_header(client, "Content-Type", content_type);
  }

  int body_len = body != NULL ? (int)strlen(body) : 0;
  esp_err_t err = esp_http_client_open(client, body_len);
  if (err != ESP_OK)
  {
    esp_http_client_cleanup(client);
    return err;
  }

  if (body_len > 0)
  {
    int written = esp_http_client_write(client, body, body_len);
    if (written != body_len)
    {
      esp_http_client_cleanup(client);
      return ESP_FAIL;
    }
  }

  esp_http_client_fetch_headers(client);
  *status_code = esp_http_client_get_status_code(client);
  err = read_http_body(client, out_body, out_len);
  esp_http_client_cleanup(client);
  return err;
}

__attribute__((unused)) static esp_err_t ota_report_device_version(const char *authorization,
                                                                   const char *dev_id,
                                                                   const char *version)
{
  if (authorization == NULL || authorization[0] == '\0' || dev_id == NULL ||
      dev_id[0] == '\0' || version == NULL || version[0] == '\0')
  {
    return ESP_ERR_INVALID_ARG;
  }

  char url[256] = {0};
  char body[96] = {0};
  int ret = snprintf(url, sizeof(url),
                     "http://ota.heclouds.com/ota/device/version?dev_id=%s",
                     dev_id);
  if (ret < 0 || ret >= (int)sizeof(url))
  {
    return ESP_ERR_INVALID_SIZE;
  }
  ret = snprintf(body, sizeof(body), "{\"s_version\":\"%s\"}", version);
  if (ret < 0 || ret >= (int)sizeof(body))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  int status_code = 0;
  char *resp_body = NULL;
  size_t resp_len = 0;
  ESP_LOGI(TAG, "Reporting OTA device version: %s body=%s", url, body);
  esp_err_t err = ota_http_request(url, HTTP_METHOD_POST, authorization,
                                   "application/json", body, 4000, &status_code,
                                   &resp_body, &resp_len);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "OTA version report request failed: %s", esp_err_to_name(err));
    free(resp_body);
    return err;
  }

  ESP_LOGI(TAG, "OTA version report status=%d, body=%s", status_code,
           resp_body != NULL ? resp_body : "");
  if (status_code < 200 || status_code >= 300 || resp_body == NULL ||
      resp_len == 0)
  {
    free(resp_body);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_body);
  free(resp_body);
  if (root == NULL)
  {
    return ESP_FAIL;
  }

  cJSON *errno_item = cJSON_GetObjectItem(root, "errno");
  cJSON *error_item = cJSON_GetObjectItem(root, "error");
  esp_err_t result = ESP_OK;
  if (!cJSON_IsNumber(errno_item) || errno_item->valueint != 0)
  {
    ESP_LOGW(TAG, "OTA version report returned errno=%d, error=%s",
             cJSON_IsNumber(errno_item) ? errno_item->valueint : -1,
             cJSON_IsString(error_item) ? error_item->valuestring : "unknown");
    result = ESP_FAIL;
  }
  cJSON_Delete(root);
  return result;
}

typedef struct
{
  char tid[32];
  char target_version[64];
  char md5[64];
  int size;
  int package_type;
  int task_status;
} fuse_ota_task_info_t;

static esp_err_t ota_parse_fuse_check_response(const char *body,
                                               fuse_ota_task_info_t *task_info,
                                               bool *has_task,
                                               int query_type)
{
  if (body == NULL || task_info == NULL || has_task == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  memset(task_info, 0, sizeof(*task_info));
  *has_task = false;

  cJSON *root = cJSON_Parse(body);
  if (root == NULL)
  {
    ESP_LOGE(TAG, "Failed to parse fuse-ota check response");
    return ESP_FAIL;
  }

  cJSON *code_item = cJSON_GetObjectItem(root, "code");
  cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
  if (!cJSON_IsNumber(code_item))
  {
    ESP_LOGW(TAG, "fuse-ota check response did not include numeric code");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  if (code_item->valueint != 0)
  {
    ESP_LOGW(TAG, "fuse-ota check returned code=%d, msg=%s",
             code_item->valueint,
             cJSON_IsString(msg_item) ? msg_item->valuestring : "unknown");
    if (code_item->valueint == 12012)
    {
      ESP_LOGI(TAG,
               "No matching fuse-ota task: product=%s, device=%s, type=%d, "
               "version=%s。通常表示这台设备/这个版本当前没有待执行的升级任务，或者任务已被关闭",
               GW_PRODUCTID, GW_DEVICENAME, query_type,
               gateway_firmware_version());
      cJSON_Delete(root);
      return ESP_OK;
    }
    if (code_item->valueint == 12010)
    {
      ESP_LOGW(TAG,
               "fuse-ota type mismatch query=%d, trying alternate type",

               query_type);
      cJSON_Delete(root);
      return ESP_ERR_NOT_SUPPORTED;
    }
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsObject(data))
  {
    ESP_LOGI(TAG, "fuse-ota check data field is empty, no task info");
    cJSON_Delete(root);
    return ESP_OK;
  }

  cJSON *tid_item = cJSON_GetObjectItem(data, "tid");
  if (!copy_json_string_or_number(tid_item, task_info->tid,
                                  sizeof(task_info->tid)))
  {
    ESP_LOGW(TAG, "fuse-ota task data did not include a usable tid");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  copy_json_string_or_number(cJSON_GetObjectItem(data, "target"),
                             task_info->target_version,
                             sizeof(task_info->target_version));
  copy_json_string_or_number(cJSON_GetObjectItem(data, "md5"), task_info->md5,
                             sizeof(task_info->md5));
  task_info->size = cJSON_GetObjectItem(data, "size") != NULL &&
                            cJSON_IsNumber(cJSON_GetObjectItem(data, "size"))
                        ? cJSON_GetObjectItem(data, "size")->valueint
                        : 0;
  task_info->package_type =
      cJSON_GetObjectItem(data, "type") != NULL &&
              cJSON_IsNumber(cJSON_GetObjectItem(data, "type"))
          ? cJSON_GetObjectItem(data, "type")->valueint
          : 0;
  task_info->task_status =
      cJSON_GetObjectItem(data, "status") != NULL &&
              cJSON_IsNumber(cJSON_GetObjectItem(data, "status"))
          ? cJSON_GetObjectItem(data, "status")->valueint
          : 0;

  *has_task = true;
  cJSON_Delete(root);
  return ESP_OK;
}

static esp_err_t ota_report_fuse_status(const char *authorization,
                                        const char *status_url, int step)
{
  if (authorization == NULL || authorization[0] == '\0' || status_url == NULL ||
      status_url[0] == '\0')
  {
    return ESP_ERR_INVALID_ARG;
  }

  char body[32] = {0};
  int ret = snprintf(body, sizeof(body), "{\"step\":%d}", step);
  if (ret < 0 || ret >= (int)sizeof(body))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  int status_code = 0;
  char *resp_body = NULL;
  size_t resp_len = 0;
  ESP_LOGI(TAG, "Reporting fuse-ota status step=%d: %s", step, status_url);
  esp_err_t err = ota_http_request(status_url, HTTP_METHOD_POST, authorization,
                                   "application/json", body, 4000, &status_code,
                                   &resp_body, &resp_len);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "fuse-ota status report failed: %s", esp_err_to_name(err));
    free(resp_body);
    return err;
  }

  ESP_LOGI(TAG, "fuse-ota status report status=%d, body=%s", status_code,
           resp_body != NULL ? resp_body : "");
  if (status_code < 200 || status_code >= 300 || resp_body == NULL ||
      resp_len == 0)
  {
    free(resp_body);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_body);
  free(resp_body);
  if (root == NULL)
  {
    return ESP_FAIL;
  }

  cJSON *code_item = cJSON_GetObjectItem(root, "code");
  cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
  esp_err_t result = ESP_OK;
  if (!cJSON_IsNumber(code_item) || code_item->valueint != 0)
  {
    ESP_LOGW(TAG, "fuse-ota status report returned code=%d, msg=%s",
             cJSON_IsNumber(code_item) ? code_item->valueint : -1,
             cJSON_IsString(msg_item) ? msg_item->valuestring : "unknown");
    result = ESP_FAIL;
  }
  cJSON_Delete(root);
  return result;
}

static int ota_map_fuse_failure_step(esp_err_t err)
{
  if (err == ESP_ERR_NO_MEM)
  {
    return 103;
  }
  if (err == ESP_ERR_TIMEOUT)
  {
    return 104;
  }
  if (err == ESP_ERR_INVALID_CRC)
  {
    return 205;
  }
  return 107;
}

static void md5_digest_to_hex(const unsigned char digest[16], char *out_hex,
                              size_t out_hex_len)
{
  static const char hex_chars[] = "0123456789abcdef";
  if (digest == NULL || out_hex == NULL || out_hex_len < 33)
  {
    return;
  }

  for (size_t i = 0; i < 16; ++i)
  {
    out_hex[i * 2] = hex_chars[(digest[i] >> 4) & 0x0F];
    out_hex[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
  }
  out_hex[32] = '\0';
}

static void ota_pending_info_clear(void)
{
  ota_pending_info_t pending = {0};
  EEprom_WriteData(OTA_PENDING_INFO_KEY, &pending, sizeof(pending));
}

static void ota_pending_info_save(const char *status_url,
                                  const char *target_version)
{
  ota_pending_info_t pending = {0};
  pending.magic = OTA_PENDING_INFO_MAGIC;
  snprintf(pending.status_url, sizeof(pending.status_url), "%s",
           status_url != NULL ? status_url : "");
  snprintf(pending.target_version, sizeof(pending.target_version), "%s",
           target_version != NULL ? target_version : "");
  EEprom_WriteData(OTA_PENDING_INFO_KEY, &pending, sizeof(pending));
}

static bool ota_pending_info_load(ota_pending_info_t *out_pending)
{
  if (out_pending == NULL)
  {
    return false;
  }

  memset(out_pending, 0, sizeof(*out_pending));
  EEprom_ReadData(OTA_PENDING_INFO_KEY, out_pending, sizeof(*out_pending));
  return out_pending->magic == OTA_PENDING_INFO_MAGIC &&
         out_pending->status_url[0] != '\0';
}

__attribute__((unused)) __attribute__((unused)) static esp_err_t ota_query_device_id_via_devinfo(const char *authorization,
                                                                                                 char *out_dev_id,
                                                                                                 size_t out_dev_id_len)
{
  if (authorization == NULL || authorization[0] == '\0' || out_dev_id == NULL ||
      out_dev_id_len == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (OTA_NUMERIC_PID[0] == '\0')
  {
    ESP_LOGW(TAG,
             "OTA_NUMERIC_PID is empty. Skip official /ota/devInfo lookup and "
             "fall back to MQTT device API. If south/check returns errno=26, "
             "fill OTA_NUMERIC_PID from OneNET OTA product details.");
    return ESP_ERR_INVALID_STATE;
  }

  if (!looks_like_decimal_string(OTA_NUMERIC_PID))
  {
    ESP_LOGE(TAG,
             "OTA_NUMERIC_PID=%s is not a numeric pid. Copy the numeric pid "
             "from OneNET OTA product details, not the alphanumeric product_id.",
             OTA_NUMERIC_PID);
    return ESP_ERR_INVALID_ARG;
  }

  if (OTA_DEVICE_AUTHINFO[0] == '\0')
  {
    ESP_LOGE(TAG,
             "OTA_DEVICE_AUTHINFO is empty. Set it to the device authInfo used "
             "by OneNET OTA, usually the device name for MQTT/MQTTS products.");
    return ESP_ERR_INVALID_STATE;
  }

  char body[256] = {0};
  int ret = snprintf(body, sizeof(body), "{\"pid\":%s,\"authInfo\":\"%s\"}",
                     OTA_NUMERIC_PID, OTA_DEVICE_AUTHINFO);
  if (ret < 0 || ret >= (int)sizeof(body))
  {
    return ESP_ERR_INVALID_SIZE;
  }

  esp_err_t err = ESP_FAIL;
  for (size_t i = 0;
       i < sizeof(g_ota_devinfo_url_templates) /
               sizeof(g_ota_devinfo_url_templates[0]);
       ++i)
  {
    int status_code = 0;
    char *resp_body = NULL;
    size_t resp_len = 0;

    ESP_LOGI(TAG, "Querying official OTA dev_id endpoint %u: %s body=%s",
             (unsigned int)(i + 1), g_ota_devinfo_url_templates[i], body);
    err = ota_http_request(g_ota_devinfo_url_templates[i], HTTP_METHOD_GET,
                           authorization, "application/json", body, 4000,
                           &status_code, &resp_body, &resp_len);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "Official OTA dev_id request failed on endpoint %u: %s",
               (unsigned int)(i + 1), esp_err_to_name(err));
      free(resp_body);
      continue;
    }

    ESP_LOGI(TAG, "Official OTA dev_id endpoint %u status=%d, body=%s",
             (unsigned int)(i + 1), status_code,
             resp_body != NULL ? resp_body : "");
    if (status_code < 200 || status_code >= 300 || resp_body == NULL ||
        resp_len == 0)
    {
      free(resp_body);
      continue;
    }

    cJSON *root = cJSON_Parse(resp_body);
    free(resp_body);
    if (root == NULL)
    {
      continue;
    }

    cJSON *errno_item = cJSON_GetObjectItem(root, "errno");
    cJSON *error_item = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsNumber(errno_item) && errno_item->valueint != 0)
    {
      ESP_LOGW(TAG, "Official OTA dev_id lookup returned errno=%d, error=%s",
               errno_item->valueint,
               cJSON_IsString(error_item) ? error_item->valuestring : "unknown");
      if (errno_item->valueint == 2)
      {
        ESP_LOGW(TAG,
                 "Check OTA_NUMERIC_PID and OTA_DEVICE_AUTHINFO. The OTA "
                 "platform rejected one of these values.");
      }
      cJSON_Delete(root);
      continue;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *dev_id_item =
        cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "dev_id") : NULL;
    cJSON *id_item = cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "id") : NULL;
    if (copy_json_string_or_number(dev_id_item, out_dev_id, out_dev_id_len) ||
        copy_json_string_or_number(id_item, out_dev_id, out_dev_id_len) ||
        copy_json_string_or_number(data, out_dev_id, out_dev_id_len))
    {
      ESP_LOGI(TAG, "Resolved OTA dev_id via official /ota/devInfo: %s",
               out_dev_id);
      cJSON_Delete(root);
      return ESP_OK;
    }

    ESP_LOGW(TAG,
             "Official OTA dev_id lookup succeeded but response did not include "
             "a usable dev_id");
    cJSON_Delete(root);
  }

  return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
}

__attribute__((unused)) static esp_err_t ota_query_device_id(const char *authorization, char *out_dev_id,
                                                             size_t out_dev_id_len)
{
  if (authorization == NULL || out_dev_id == NULL || out_dev_id_len == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if (OTA_DEVICE_ID[0] != '\0')
  {
    snprintf(out_dev_id, out_dev_id_len, "%s", OTA_DEVICE_ID);
    ESP_LOGI(TAG, "Using configured OTA_DEVICE_ID=%s", out_dev_id);
    return ESP_OK;
  }

  char *encoded_device_name = calloc(1, 256);
  char *url = calloc(1, 384);
  if (encoded_device_name == NULL || url == NULL)
  {
    free(encoded_device_name);
    free(url);
    return ESP_ERR_NO_MEM;
  }
  URL_encode((char *)GW_DEVICENAME, strlen(GW_DEVICENAME), encoded_device_name);

  esp_err_t err = ESP_FAIL;
  for (size_t i = 0;
       i < sizeof(g_ota_device_lookup_url_templates) /
               sizeof(g_ota_device_lookup_url_templates[0]);
       ++i)
  {
    int status_code = 0;
    char *resp_body = NULL;
    size_t resp_len = 0;

    int ret = snprintf(url, 384, g_ota_device_lookup_url_templates[i],
                       encoded_device_name);
    if (ret < 0 || ret >= 384)
    {
      free(encoded_device_name);
      free(url);
      return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "Querying OTA device_id endpoint %u: %s",
             (unsigned int)(i + 1), url);
    err = ota_http_get(url, authorization, 4000, &status_code, &resp_body,
                       &resp_len);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "OTA device_id request failed on endpoint %u: %s",
               (unsigned int)(i + 1), esp_err_to_name(err));
      free(resp_body);
      continue;
    }

    ESP_LOGI(TAG, "OTA device_id endpoint %u status=%d, body=%s",
             (unsigned int)(i + 1), status_code,
             resp_body != NULL ? resp_body : "");
    if (status_code < 200 || status_code >= 300 || resp_body == NULL ||
        resp_len == 0)
    {
      free(resp_body);
      continue;
    }

    cJSON *root = cJSON_Parse(resp_body);
    free(resp_body);
    if (root == NULL)
    {
      continue;
    }

    cJSON *errno_item = cJSON_GetObjectItem(root, "errno");
    cJSON *error_item = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsNumber(errno_item) && errno_item->valueint != 0)
    {
      ESP_LOGW(TAG, "OTA dev_id returned errno=%d, error=%s",
               errno_item->valueint,
               cJSON_IsString(error_item) ? error_item->valuestring : "unknown");
      cJSON_Delete(root);
      continue;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *device_id_item =
        cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "device_id") : NULL;
    cJSON *dev_id_item =
        cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "dev_id") : NULL;
    cJSON *id_item = cJSON_IsObject(data) ? cJSON_GetObjectItem(data, "id") : NULL;
    if (copy_json_string_or_number(device_id_item, out_dev_id, out_dev_id_len) ||
        copy_json_string_or_number(dev_id_item, out_dev_id, out_dev_id_len) ||
        copy_json_string_or_number(id_item, out_dev_id, out_dev_id_len))
    {
      cJSON_Delete(root);
      ESP_LOGI(TAG, "Resolved OTA device_id via MQTT device API: %s",
               out_dev_id);
      free(encoded_device_name);
      free(url);
      return ESP_OK;
    }

    cJSON *code_no_item = cJSON_GetObjectItem(root, "code_no");
    cJSON *code_item = cJSON_GetObjectItem(root, "code");
    cJSON *message_item = cJSON_GetObjectItem(root, "message");
    cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
    const char *message =
        json_string_or_fallback(message_item, msg_item, "unknown");
    ESP_LOGW(TAG,
             "MQTT device API returned no device_id (code_no=%s, code=%s, "
             "message=%s)",
             cJSON_IsString(code_no_item) ? code_no_item->valuestring : "n/a",
             cJSON_IsString(code_item) ? code_item->valuestring : "n/a",
             message);
    if (cJSON_IsString(code_item) &&
        strcmp(code_item->valuestring, "onenet_common_authFailed") == 0)
    {
      ESP_LOGW(TAG,
               "MQTT device API auth failed. This endpoint requires the MQTT "
               "product access_key token, not OTA_PRODUCT_ACCESSKEY and not "
               "the user Accesskey.");
    }
    cJSON_Delete(root);
  }

  ESP_LOGE(TAG,
           "Failed to resolve OTA device_id from MQTT device API for product=%s "
           "device=%s. Configure OTA_DEVICE_ID only if auto lookup keeps "
           "failing.",
           GW_PRODUCTID, GW_DEVICENAME);
  free(encoded_device_name);
  free(url);
  return err != ESP_OK ? err : ESP_ERR_NOT_FOUND;
}

static esp_err_t perform_streaming_ota_once(const char *url, bool add_range,
                                            bool *out_zero_byte)
{
  if (url == NULL || url[0] == '\0')
  {
    return ESP_ERR_INVALID_ARG;
  }
  if (out_zero_byte != NULL)
  {
    *out_zero_byte = false;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 20000,
      .keep_alive_enable = false,
      .buffer_size = 4096,
      .buffer_size_tx = 1024,
      .addr_type = HTTP_ADDR_TYPE_INET,
  };
  if (ota_url_is_https(url))
  {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL)
  {
    return ESP_ERR_NO_MEM;
  }

  if (g_ota_download_auth[0] != '\0')
  {
    esp_http_client_set_header(client, "Authorization", g_ota_download_auth);
  }
  if (add_range)
  {
    esp_http_client_set_header(client, "Range", "0-");
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK)
  {
    esp_http_client_cleanup(client);
    return err;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  if (status_code != 200 && status_code != 206)
  {
    ESP_LOGE(TAG, "OTA download HTTP status=%d", status_code);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  int expected_size = g_ota_expected_size > 0 ? g_ota_expected_size : content_length;
  char *header_value = NULL;
  const char *ota_errno = NULL;
  const char *content_range = NULL;
  const char *transfer_encoding = NULL;
  const char *content_type = NULL;
  if (esp_http_client_get_header(client, "Ota-Errno", &header_value) == ESP_OK &&
      header_value != NULL)
  {
    ota_errno = header_value;
  }
  if (esp_http_client_get_header(client, "Content-Range", &header_value) == ESP_OK &&
      header_value != NULL)
  {
    content_range = header_value;
  }
  if (esp_http_client_get_header(client, "Transfer-Encoding", &header_value) ==
          ESP_OK &&
      header_value != NULL)
  {
    transfer_encoding = header_value;
  }
  if (esp_http_client_get_header(client, "Content-Type", &header_value) == ESP_OK &&
      header_value != NULL)
  {
    content_type = header_value;
  }
  bool is_chunked = esp_http_client_is_chunked_response(client);
  ESP_LOGI(TAG,
           "OTA download headers: status=%d http_content_length=%d expected_size=%d "
           "chunked=%d ota_errno=%s content_range=%s content_type=%s transfer_encoding=%s",
           status_code, content_length, expected_size, is_chunked ? 1 : 0,
           ota_errno != NULL ? ota_errno : "n/a",
           content_range != NULL ? content_range : "n/a",
           content_type != NULL ? content_type : "n/a",
           transfer_encoding != NULL ? transfer_encoding : "n/a");
  if (ota_errno != NULL && strcmp(ota_errno, "0") != 0)
  {
    ESP_LOGE(TAG, "OTA server rejected download, Ota-Errno=%s", ota_errno);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }
  if (content_length > 0 && expected_size > 0 && content_length != expected_size)
  {
    ESP_LOGW(TAG,
             "OTA size hint mismatch between HTTP header and task info: header=%d task=%d",
             content_length, expected_size);
  }

  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL)
  {
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  esp_ota_handle_t ota_handle = 0;
  bool ota_started = false;
  err = esp_ota_begin(update_partition,
                      expected_size > 0 ? expected_size : OTA_SIZE_UNKNOWN,
                      &ota_handle);
  if (err != ESP_OK)
  {
    esp_http_client_cleanup(client);
    return err;
  }
  ota_started = true;

  uint8_t *buffer = malloc(4096);
  if (buffer == NULL)
  {
    esp_ota_abort(ota_handle);
    esp_http_client_cleanup(client);
    return ESP_ERR_NO_MEM;
  }

  iot_md5_context md5_ctx;
  bool md5_enabled = (g_ota_expected_md5[0] != '\0');
  if (md5_enabled)
  {
    utils_md5_init(&md5_ctx);
    utils_md5_starts(&md5_ctx);
  }

  size_t total_written = 0;
  int read_iterations = 0;
  while (1)
  {
    int read_len = esp_http_client_read(client, (char *)buffer, 4096);
    if (read_len < 0)
    {
      err = ESP_FAIL;
      break;
    }
    if (read_len == 0)
    {
      ESP_LOGI(TAG, "OTA download stream reached EOF after %d reads, total=%u bytes",
               read_iterations, (unsigned int)total_written);
      err = ESP_OK;
      break;
    }
    ++read_iterations;
    if (read_iterations <= 3 || (read_iterations % 64) == 0 ||
        (total_written + (size_t)read_len) >= (size_t)expected_size)
    {
      ESP_LOGI(TAG, "OTA read #%d: %d bytes, total=%u/%d",
               read_iterations, read_len,
               (unsigned int)(total_written + (size_t)read_len), expected_size);
    }

    err = esp_ota_write(ota_handle, buffer, (size_t)read_len);
    if (err != ESP_OK)
    {
      break;
    }
    if (md5_enabled)
    {
      utils_md5_update(&md5_ctx, buffer, (size_t)read_len);
    }
    total_written += (size_t)read_len;
  }

  free(buffer);
  esp_http_client_cleanup(client);

  if (err != ESP_OK)
  {
    if (ota_started)
    {
      esp_ota_abort(ota_handle);
    }
    return err;
  }

  if (total_written == 0)
  {
    if (out_zero_byte != NULL)
    {
      *out_zero_byte = true;
    }
    ESP_LOGE(TAG,
             "OTA download returned zero bytes. Check Ota-Errno, task state, and "
             "whether the server expects Range-based download.");
    esp_ota_abort(ota_handle);
    return ESP_ERR_INVALID_SIZE;
  }

  if (expected_size > 0 && total_written != (size_t)expected_size)
  {
    ESP_LOGE(TAG, "OTA size mismatch: expected=%d actual=%u", expected_size,
             (unsigned int)total_written);
    esp_ota_abort(ota_handle);
    return ESP_ERR_INVALID_SIZE;
  }

  if (md5_enabled)
  {
    unsigned char digest[16];
    char digest_hex[33] = {0};
    utils_md5_finish(&md5_ctx, digest);
    utils_md5_free(&md5_ctx);
    md5_digest_to_hex(digest, digest_hex, sizeof(digest_hex));
    if (strcasecmp(digest_hex, g_ota_expected_md5) != 0)
    {
      ESP_LOGE(TAG, "OTA md5 mismatch: expected=%s actual=%s", g_ota_expected_md5,
               digest_hex);
      esp_ota_abort(ota_handle);
      return ESP_ERR_INVALID_CRC;
    }
    ESP_LOGI(TAG, "OTA md5 verified: %s", digest_hex);
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK)
  {
    return err;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK)
  {
    return err;
  }

  ESP_LOGI(TAG, "OTA image written successfully: %u bytes",
           (unsigned int)total_written);
  return ESP_OK;
}

static esp_err_t perform_streaming_ota(const char *url)
{
  bool zero_byte = false;
  esp_err_t err = perform_streaming_ota_once(url, true, &zero_byte);
  if (err == ESP_OK || !zero_byte)
  {
    return err;
  }

  ESP_LOGW(TAG,
           "OTA download returned zero bytes with Range header, retrying without Range: %s",
           url);
  zero_byte = false;
  err = perform_streaming_ota_once(url, false, &zero_byte);
  if (err == ESP_OK || !zero_byte)
  {
    return err;
  }

  if (strncmp(url, "https://", strlen("https://")) == 0)
  {
    char fallback_url[768] = {0};
    snprintf(fallback_url, sizeof(fallback_url), "http://%s",
             url + strlen("https://"));
    ESP_LOGW(TAG,
             "OTA download still returned zero bytes, retrying over HTTP without Range: %s",
             fallback_url);
    zero_byte = false;
    err = perform_streaming_ota_once(fallback_url, false, &zero_byte);
  }

  return err;
}

#if 0
void Studio_OTA_CheckTask(void) {
  extern char Mqtt_Password[]; // 复用 MQTT 连接?Token 进行鉴权

  char url[512];
  snprintf(url, sizeof(url),
           "https://studio-ota.heclouds.com/ota/south/check?product_id=%s&"
           "device_name=%s&version=%s",
           GW_PRODUCTID, GW_DEVICENAME, CURRENT_FW_VERSION);

  ESP_LOGI(TAG, "OTA log message");

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 15000,
      .skip_cert_common_name_check = true,
      .use_global_ca_store = false,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return;

  esp_http_client_set_header(client, "Authorization", Mqtt_Password);

  esp_err_t err = esp_http_client_open(client, 0);
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    int content_length = esp_http_client_get_content_length(client);

    if (content_length > 0 && content_length < 4096) {
      char *buffer = malloc(content_length + 1);
      if (buffer) {
        int read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len > 0) {
          buffer[read_len] = '\0';
          ESP_LOGI(TAG, "OTA log message");

          cJSON *root = cJSON_Parse(buffer);
          if (root) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data && cJSON_IsObject(data)) {
              cJSON *url_item = cJSON_GetObjectItem(data, "url");

              if (url_item && cJSON_IsString(url_item)) {
                const char *download_url = url_item->valuestring;
                ESP_LOGI(TAG, "OTA log message");
                WiFi_Cat1_StartOTA(download_url, NULL, 0);
              }
            }
            cJSON_Delete(root);
          }
        }
        free(buffer);
      }
    }
  }
  esp_http_client_cleanup(client);
}

#endif

void Studio_OTA_CheckTask(void)
{
  extern char Mqtt_Password[];

  typedef struct
  {
    char url[512];
    char encoded_device_name[256];
    char download_url[512];
    char status_url[512];
    fuse_ota_task_info_t task_info;
  } ota_check_ctx_t;

  if (!(SysCB.SysEventFlag & CONNECT_WIFI))
  {
    ESP_LOGW(TAG, "Skip OTA check because WiFi is not connected");
    return;
  }
  if (!(SysCB.SysEventFlag & CONNECT_MQTT))
  {
    ESP_LOGW(TAG, "Skip OTA check because MQTT is not connected yet");
    return;
  }
  if (SysCB.SysEventFlag & OTA_RUNNING)
  {
    ESP_LOGW(TAG, "Skip OTA check because OTA is already running");
    return;
  }
  if (Mqtt_Password[0] == '\0')
  {
    ESP_LOGE(TAG, "MQTT device token is empty, cannot call fuse-ota APIs");
    return;
  }

  ota_check_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL)
  {
    ESP_LOGE(TAG, "Failed to allocate OTA check context");
    return;
  }

  URL_encode((char *)GW_DEVICENAME, strlen(GW_DEVICENAME), ctx->encoded_device_name);

  ESP_LOGI(TAG, "开始检查 OTA 任务，当前 firmware_version=%s",
           gateway_firmware_version());
  ESP_LOGI(TAG, "Studio_OTA_CheckTask stack watermark=%u",
           (unsigned int)uxTaskGetStackHighWaterMark(NULL));

  char *buffer = NULL;
  size_t response_len = 0;
  int status_code = 0;
  esp_err_t err = ESP_FAIL;
  bool has_task = false;
  const int request_timeout_ms = 4000;
  int query_types[2] = {OTA_SOUTH_TYPE, OTA_SOUTH_TYPE == 1 ? 2 : 1};
  size_t query_type_count =
      query_types[0] == query_types[1] ? 1U : 2U;

  for (size_t type_idx = 0; type_idx < query_type_count; ++type_idx)
  {
    int query_type = query_types[type_idx];
    bool type_mismatch_seen = false;

    for (size_t i = 0;
         i < sizeof(g_fuse_ota_check_url_templates) /
                 sizeof(g_fuse_ota_check_url_templates[0]);
         ++i)
    {
      snprintf(ctx->url, sizeof(ctx->url), g_fuse_ota_check_url_templates[i],
               GW_PRODUCTID, ctx->encoded_device_name, query_type,
               gateway_firmware_version());
      ESP_LOGI(TAG, "Checking fuse-ota endpoint %u (type=%d): %s",
               (unsigned int)(i + 1), query_type, ctx->url);
      log_ota_network_snapshot("before fuse-ota check");

      free(buffer);
      buffer = NULL;
      response_len = 0;
      status_code = 0;

      err = ota_http_get(ctx->url, Mqtt_Password, request_timeout_ms, &status_code,
                         &buffer, &response_len);
      if (err != ESP_OK)
      {
        ESP_LOGW(TAG, "fuse-ota check request failed on endpoint %u: %s",
                 (unsigned int)(i + 1), esp_err_to_name(err));
        continue;
      }

      ESP_LOGI(TAG, "fuse-ota endpoint %u status=%d, body=%s",
               (unsigned int)(i + 1), status_code, buffer != NULL ? buffer : "");
      if (status_code < 200 || status_code >= 300 || buffer == NULL ||
          response_len == 0)
      {
        continue;
      }

      err = ota_parse_fuse_check_response(buffer, &ctx->task_info, &has_task,
                                          query_type);
      if (err == ESP_ERR_NOT_SUPPORTED)
      {
        type_mismatch_seen = true;
        continue;
      }
      if (err == ESP_OK)
      {
        break;
      }
    }

    if (err == ESP_OK)
    {
      break;
    }

    if (type_mismatch_seen && (type_idx + 1U) < query_type_count)
    {
      ESP_LOGW(TAG,
               "fuse-ota 查询 type=%d 未匹配平台任务，继续尝试 type=%d",
               query_type, query_types[type_idx + 1U]);
    }
  }

  free(buffer);
  buffer = NULL;

  if (err != ESP_OK || status_code < 200 || status_code >= 300)
  {
    ESP_LOGW(TAG, "所有 fuse-ota 检查接口都没有返回可用结果");
    goto cleanup;
  }
  if (!has_task)
  {
    goto cleanup;
  }

  snprintf(ctx->download_url, sizeof(ctx->download_url),
           g_fuse_ota_download_url_template, GW_PRODUCTID,
           ctx->encoded_device_name, ctx->task_info.tid);
  snprintf(ctx->status_url, sizeof(ctx->status_url), g_fuse_ota_status_url_template,
           GW_PRODUCTID, ctx->encoded_device_name, ctx->task_info.tid);

  ESP_LOGI(TAG,
           "fuse-ota task found: tid=%s target=%s size=%d type=%d status=%d md5=%s",
           ctx->task_info.tid,
           ctx->task_info.target_version[0] != '\0'
               ? ctx->task_info.target_version
               : "n/a",
           ctx->task_info.size, ctx->task_info.package_type,
           ctx->task_info.task_status,
           ctx->task_info.md5[0] != '\0' ? ctx->task_info.md5 : "n/a");

  if (ctx->task_info.task_status == 3)
  {
    if (ctx->task_info.target_version[0] != '\0' &&
        strcmp(ctx->task_info.target_version, gateway_firmware_version()) != 0)
    {
      esp_err_t report_err = ESP_FAIL;
      ESP_LOGW(TAG,
               "fuse-ota task is already in upgrading state, but current "
               "firmware_version=%s does not match target=%s. The old image "
               "should close this stale task with step=206 instead of "
               "pretending the upgrade succeeded.",
               gateway_firmware_version(), ctx->task_info.target_version);
      report_err = ota_report_fuse_status(Mqtt_Password, ctx->status_url, 206);
      if (report_err == ESP_OK)
      {
        ota_pending_info_clear();
      }
    }
    else
    {
      ESP_LOGI(TAG,
               "fuse-ota task is already in upgrading state and current "
               "firmware_version=%s matches target=%s. Leave step=201 to the "
               "post-boot success path only.",
               gateway_firmware_version(),
               ctx->task_info.target_version[0] != '\0'
                   ? ctx->task_info.target_version
                   : "n/a");
    }
    goto cleanup;
  }

  snprintf(g_ota_status_url, sizeof(g_ota_status_url), "%s", ctx->status_url);
  snprintf(g_ota_target_version, sizeof(g_ota_target_version), "%s",
           ctx->task_info.target_version);
  g_ota_expected_size = ctx->task_info.size;
  snprintf(g_ota_expected_md5, sizeof(g_ota_expected_md5), "%s",
           ctx->task_info.md5);
  WiFi_Cat1_StartOTA(ctx->download_url, Mqtt_Password, 0);

cleanup:
  free(ctx);
}

void OTAServer_process(uint8_t *data, uint16_t datalen, uint32_t page_index,
                       uint8_t is_last, uint8_t ota_staflag)
{
  /* Acquire from pre-allocated pool (zero fragmentation) */
  OTA_ZC_Chunk *new_chunk = ota_zc_pool_acquire(datalen);
  if (new_chunk == NULL)
  {
    ESP_LOGE(TAG, "ZC OTA: pool exhausted (datalen=%u), dropping chunk", datalen);
    g_ota_zc_stats.enqueue_fail++;
    return;
  }
  new_chunk->page_index = page_index;
  new_chunk->is_last = is_last;
  new_chunk->ota_staflag = ota_staflag;
  memcpy(new_chunk->data, data, datalen);

  g_ota_zc_stats.enqueued++;
  if (xQueueSend(OTA_ZC_Queue, &new_chunk,
                 pdMS_TO_TICKS(OTA_ZC_SEND_TIMEOUT_MS)) != pdTRUE)
  {
    g_ota_zc_stats.enqueue_blocked++;
    ota_zc_pool_release(new_chunk);
  }
}

#if 0
void WiFi_Cat1_PropertyVersion(uint8_t num) {
  // 不再上报子设备版本信息
  if (num != 0) {
    return;
  }

  char versionatabuff[128]; // 临时缓冲?
  char tempdatabuff[512];   // 临时缓冲?(加大以防止溢?

  memset(versionatabuff, 0, 128);
  memset(tempdatabuff, 0, 512);

  // 网关版本报备逻辑 (保留 num == 0 分支)
  sprintf(versionatabuff, "{\"s_version\":\"%s\",\"f_version\": \"null\"}",
          info.Version[0]);
  sprintf(tempdatabuff,
          "POST /fuse-ota/%s/%s/version HTTP/1.1\r\nContent-Type: "
          "application/json\r\nAuthorization:%s\r\nhost:iot-api.heclouds.com"
          "\r\nContent-Length:%d\r\n\r\n%s\r\n\r\n",
          GW_PRODUCTID, GW_DEVICENAME, Accesskey, (int)strlen(versionatabuff),
          versionatabuff);

  if (SysCB.SysEventFlag & CONNECT_WIFI) {
    ESP_LOGI(TAG, "OTA log message");
  } else if (SysCB.SysEventFlag & CONNECT_CAT1) {
    bsp_uart_cat1_send(tempdatabuff, strlen(tempdatabuff));
  }
}

#endif

void WiFi_Cat1_PropertyVersion(uint8_t num)
{
  if (num != 0)
  {
    return;
  }

  if (!(SysCB.SysEventFlag & CONNECT_MQTT))
  {
    ESP_LOGW(TAG, "MQTT not connected, skip firmware version report");
    return;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
  {
    return;
  }

  cJSON_AddStringToObject(root, "id", "gw_fw_ver");
  cJSON_AddStringToObject(root, "version", "1.0");

  cJSON *params = cJSON_AddObjectToObject(root, "params");
  cJSON *fw_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_FIRMWARE_VER);
  cJSON_AddStringToObject(fw_obj, "value", gateway_firmware_version());

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data != NULL)
  {
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    if (Cat1_AT_MqttPublish(topic, post_data) == ESP_OK)
    {
      ESP_LOGI(TAG, "已上报 firmware_version=%s",
               gateway_firmware_version());
    }
    else
    {
      ESP_LOGE(TAG, "Failed to report firmware_version");
    }
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_ReportBootOtaResult(void)
{
  extern char Mqtt_Password[];

  if (!(SysCB.SysEventFlag & CONNECT_MQTT))
  {
    return;
  }
  if (Mqtt_Password[0] == '\0')
  {
    ESP_LOGW(TAG, "MQTT token empty, skip OTA success report");
    return;
  }

  ota_pending_info_t pending;
  if (!ota_pending_info_load(&pending))
  {
    return;
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running != NULL)
  {
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
      esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
      if (mark_err == ESP_OK)
      {
        ESP_LOGI(TAG, "重启成功，已将 OTA 镜像标记为有效");
      }
      else
      {
        ESP_LOGW(TAG, "标记 OTA 镜像为有效失败: %s",
                 esp_err_to_name(mark_err));
        return;
      }
    }
  }

  if (pending.target_version[0] != '\0' &&
      strcmp(pending.target_version, gateway_firmware_version()) != 0)
  {
    ESP_LOGW(TAG,
             "待确认 OTA 目标版本=%s，但当前 firmware_version=%s；暂不上报 201",
             pending.target_version, gateway_firmware_version());
    return;
  }

  esp_err_t err =
      ota_report_fuse_status(Mqtt_Password, pending.status_url, 201);
  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "已上报 OTA 升级成功，firmware_version=%s",
             gateway_firmware_version());
    ota_pending_info_clear();
  }
  else
  {
    ESP_LOGW(TAG, "上报 OTA 升级成功失败，下次启动时继续重试");
  }
}

void WiFi_Cat1_StartOTA(const char *url, const char *token,
                        uint8_t ota_staflag)
{
  if (mem_guard_ota_blocked())
  {
    ESP_LOGE(TAG, "OTA blocked: critically low memory");
    return;
  }

  if (url == NULL || url[0] == '\0')
  {
    ESP_LOGE(TAG, "OTA 下载地址为空");
    return;
  }

  if (ota_staflag != 0)
  {
    ESP_LOGW(TAG, "当前 ESP-IDF OTA 流程暂不支持子设备 OTA");
    return;
  }

  if (SysCB.SysEventFlag & OTA_RUNNING)
  {
    ESP_LOGW(TAG, "OTA 已在进行中，忽略重复请求");
    return;
  }

  snprintf(g_ota_download_auth, sizeof(g_ota_download_auth), "%s",
           token != NULL ? token : "");

  SysCB.SysEventFlag |= OTA_RUNNING;
  SysCB.SysEventFlag |= CONNECT_OTA;
  ESP_LOGI(TAG, "开始网关 OTA 下载: %s", url);
  ota_zc_subsystem_start();
  log_ota_network_snapshot("before ota download");

  esp_err_t err = perform_streaming_ota(url);
  if (err != ESP_OK)
  {
    if (g_ota_status_url[0] != '\0' && g_ota_download_auth[0] != '\0')
    {
      ota_report_fuse_status(g_ota_download_auth, g_ota_status_url,
                             ota_map_fuse_failure_step(err));
    }
    g_ota_download_auth[0] = '\0';
    g_ota_status_url[0] = '\0';
    g_ota_target_version[0] = '\0';
    g_ota_expected_size = 0;
    g_ota_expected_md5[0] = '\0';
    SysCB.SysEventFlag &= ~CONNECT_OTA;
    SysCB.SysEventFlag &= ~OTA_RUNNING;
    log_ota_network_snapshot("ota download failure");
    ESP_LOGE(TAG, "网关 OTA 失败: %s", esp_err_to_name(err));
    return;
  }

  if (g_ota_status_url[0] != '\0' && g_ota_download_auth[0] != '\0')
  {
    ota_report_fuse_status(g_ota_download_auth, g_ota_status_url, 100);
    ota_pending_info_save(g_ota_status_url, g_ota_target_version);
  }
  g_ota_download_auth[0] = '\0';
  g_ota_status_url[0] = '\0';
  g_ota_target_version[0] = '\0';
  g_ota_expected_size = 0;
  g_ota_expected_md5[0] = '\0';
  SysCB.SysEventFlag &= ~CONNECT_OTA;

  ota_zc_subsystem_stop();
  ESP_LOGI(TAG, "网关 OTA 已完成，准备重启进入新固件...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

/* OTA notify/reboot coordination stubs */
static bool g_ota_notify_pending = false;

void WiFi_Cat1_RequestOtaNotifyReboot(void)
{
  g_ota_notify_pending = true;
  ESP_LOGI(TAG, "OTA notify reboot requested");
}

bool WiFi_Cat1_BeginPendingOtaNotifyBootstrap(void)
{
  if (g_ota_notify_pending)
  {
    g_ota_notify_pending = false;
    ESP_LOGI(TAG, "OTA notify bootstrap started");
    return true;
  }
  return false;
}

void WiFi_Cat1_FinishOtaNotifyBootstrap(void)
{
  ESP_LOGI(TAG, "OTA notify bootstrap finished");
}

bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void)
{
  return g_ota_notify_pending;
}

void OneNET_FuseOTA_CheckTask(void)
{
  Studio_OTA_CheckTask();
}

void WiFi_Cat1_CheckOTATask(uint8_t num)
{
  if (num == 0)
  {
    Studio_OTA_CheckTask();
  }
}
