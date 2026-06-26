#include "wifi_cat1.h"
#include "app_config.h"
#include "bsp_storage.h"
#include "bsp_uart.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mqtt.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "utils_md5.h"

static char g_at_rx_buffer[1024] = {0};        // 共享的接收缓冲区
static volatile bool g_at_data_ready = false;  // 数据到达标志位
static SemaphoreHandle_t g_at_rx_mutex = NULL; // 互斥锁，防止读写冲突

// 前向声明，解决编译顺序问题
static esp_err_t Cat1_Send_AT_Command(const char *cmd, uint32_t timeout_ms,
                                      const char *expected_resp);

// 新增：OTA 下载控制状态 (保留结构定义供参考)
/*
static struct {
  uint32_t current_page;
  uint32_t total_size;
  uint8_t ota_staflag;
  volatile bool is_downloading;
} g_ota_at_ctrl = {0};
*/

Pack_CB pack;
static const char *WIFI_CAT1_TAG = "WIFI_CAT1";
static const char *OTA_TAG = "OTA";
#define TAG WIFI_CAT1_TAG
QueueHandle_t OTA_ZC_Queue = NULL;
static char g_ota_download_auth[512] = {0};
static char g_ota_status_url[512] = {0};
static char g_ota_target_version[64] = {0};
static int g_ota_expected_size = 0;
static char g_ota_expected_md5[64] = {0};
static const char *const g_fuse_ota_check_url_templates[] = {
    "https://iot-api.heclouds.com/fuse-ota/%s/%s/check?type=%d&version=%s",
    "http://iot-api.heclouds.com/fuse-ota/%s/%s/check?type=%d&version=%s",
};
static const char *g_fuse_ota_download_url_template =
    "https://iot-api.heclouds.com/fuse-ota/%s/%s/%s/download";
static const char *g_fuse_ota_status_url_template =
    "https://iot-api.heclouds.com/fuse-ota/%s/%s/%s/status";

#define OTA_PENDING_INFO_MAGIC 0x4F544132UL
#define OTA_PENDING_INFO_KEY "ota_pending"
#define OTA_NOTIFY_REBOOT_MAGIC 0x4F544152UL
#define OTA_NOTIFY_REBOOT_KEY "ota_reboot"

typedef struct {
  uint32_t magic;
  char status_url[512];
  char target_version[64];
} ota_pending_info_t;

typedef struct {
  uint32_t magic;
} ota_notify_reboot_info_t;

static bool s_ota_notify_bootstrap_active = false;
/* ═══════════════════════════════════════════════════════════════
 * OTA ZC 预分配内存池
 * 一次 malloc N 个固定大小 chunk，用完归还，永不 free
 * ═══════════════════════════════════════════════════════════════ */
#define OTA_ZC_CHUNK_SIZE (sizeof(OTA_ZC_Chunk) + OTA_ZC_CHUNK_DATA_MAX)

typedef struct {
  OTA_ZC_Chunk *chunks[OTA_ZC_POOL_SIZE];
  int free_stack[OTA_ZC_POOL_SIZE];
  int free_count;
  SemaphoreHandle_t mutex;
  bool initialized;
} OTA_ZC_Pool;

static OTA_ZC_Pool g_ota_zc_pool = { .initialized = false };

void ota_zc_pool_init(void)
{
  if (g_ota_zc_pool.initialized) return;
  g_ota_zc_pool.mutex = xSemaphoreCreateMutex();
  if (g_ota_zc_pool.mutex == NULL) {
    ESP_LOGE(OTA_TAG, "ota_zc_pool: mutex create failed");
    return;
  }
  int allocated = 0;
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++) {
    g_ota_zc_pool.chunks[i] =
        (OTA_ZC_Chunk *)heap_caps_malloc(OTA_ZC_CHUNK_SIZE, MALLOC_CAP_SPIRAM);
    if (g_ota_zc_pool.chunks[i] == NULL) {
      g_ota_zc_pool.chunks[i] =
          (OTA_ZC_Chunk *)heap_caps_malloc(OTA_ZC_CHUNK_SIZE, MALLOC_CAP_INTERNAL);
    }
    if (g_ota_zc_pool.chunks[i] == NULL) break;
    allocated++;
  }
  if (allocated == 0) {
    ESP_LOGE(OTA_TAG, "ota_zc_pool: no memory for chunks");
    vSemaphoreDelete(g_ota_zc_pool.mutex);
    g_ota_zc_pool.mutex = NULL;
    return;
  }
  for (int i = 0; i < allocated; i++) {
    g_ota_zc_pool.free_stack[i] = i;
  }
  g_ota_zc_pool.free_count = allocated;
  g_ota_zc_pool.initialized = true;
  ESP_LOGI(OTA_TAG, "ota_zc_pool init: %d chunks (%u bytes each)",
           allocated, (unsigned int)OTA_ZC_CHUNK_SIZE);
}

void ota_zc_pool_deinit(void)
{
  if (!g_ota_zc_pool.initialized) return;
  if (g_ota_zc_pool.mutex) {
    xSemaphoreTake(g_ota_zc_pool.mutex, portMAX_DELAY);
  }
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++) {
    if (g_ota_zc_pool.chunks[i]) {
      free(g_ota_zc_pool.chunks[i]);
      g_ota_zc_pool.chunks[i] = NULL;
    }
  }
  g_ota_zc_pool.free_count = 0;
  g_ota_zc_pool.initialized = false;
  if (g_ota_zc_pool.mutex) {
    xSemaphoreGive(g_ota_zc_pool.mutex);
    vSemaphoreDelete(g_ota_zc_pool.mutex);
    g_ota_zc_pool.mutex = NULL;
  }
  ESP_LOGI(OTA_TAG, "ota_zc_pool deinit");
}

OTA_ZC_Chunk *ota_zc_pool_acquire(uint16_t datalen)
{
  if (!g_ota_zc_pool.initialized || datalen > OTA_ZC_CHUNK_DATA_MAX) {
    return NULL;
  }
  OTA_ZC_Chunk *chunk = NULL;
  if (g_ota_zc_pool.mutex) {
    xSemaphoreTake(g_ota_zc_pool.mutex, pdMS_TO_TICKS(500));
  }
  if (g_ota_zc_pool.free_count > 0) {
    int idx = g_ota_zc_pool.free_stack[--g_ota_zc_pool.free_count];
    chunk = g_ota_zc_pool.chunks[idx];
    chunk->len = datalen;
  }
  if (g_ota_zc_pool.mutex) {
    xSemaphoreGive(g_ota_zc_pool.mutex);
  }
  return chunk;
}

void ota_zc_pool_release(OTA_ZC_Chunk *chunk)
{
  if (!g_ota_zc_pool.initialized || chunk == NULL) return;
  if (g_ota_zc_pool.mutex) {
    xSemaphoreTake(g_ota_zc_pool.mutex, pdMS_TO_TICKS(500));
  }
  for (int i = 0; i < OTA_ZC_POOL_SIZE; i++) {
    if (g_ota_zc_pool.chunks[i] == chunk) {
      if (g_ota_zc_pool.free_count < OTA_ZC_POOL_SIZE) {
        g_ota_zc_pool.free_stack[g_ota_zc_pool.free_count++] = i;
      }
      break;
    }
  }
  if (g_ota_zc_pool.mutex) {
    xSemaphoreGive(g_ota_zc_pool.mutex);
  }
}


const char *WiFi_Cat1_GetRuntimeFirmwareVersion(void) {
  const esp_app_desc_t *app_desc = esp_app_get_description();
  if (app_desc != NULL && app_desc->version[0] != '\0') {
    return app_desc->version;
  }
  return CURRENT_FW_VERSION;
}

static const char *gateway_firmware_version(void) {
  return WiFi_Cat1_GetRuntimeFirmwareVersion();
}

static bool ota_url_is_https(const char *url) {
  return url != NULL && strncmp(url, "https://", strlen("https://")) == 0;
}

static bool copy_json_string_or_number(cJSON *item, char *out, size_t out_len) {
  if (item == NULL || out == NULL || out_len == 0) {
    return false;
  }

  if (cJSON_IsString(item) && item->valuestring != NULL &&
      item->valuestring[0] != '\0') {
    int ret = snprintf(out, out_len, "%s", item->valuestring);
    return ret > 0 && ret < (int)out_len;
  }

  if (cJSON_IsNumber(item)) {
    int ret = snprintf(out, out_len, "%.0f", item->valuedouble);
    return ret > 0 && ret < (int)out_len;
  }

  return false;
}

#undef TAG
#define TAG OTA_TAG

/* Legacy OTA response parser removed. */
#if 0

  cJSON *errno_item = cJSON_GetObjectItem(root, "errno");
  cJSON *error_item = cJSON_GetObjectItem(root, "error");
  if (cJSON_IsNumber(errno_item)) {
    if (errno_item->valueint == 11) {
      ESP_LOGI(TAG, "当前固件版本没有可执行的 OTA 任务");
      cJSON_Delete(root);
      return OTA_CHECK_PARSE_NO_TASK;
    }
    if (errno_item->valueint != 0) {
      ESP_LOGW(TAG, "OTA check returned errno=%d, error=%s",
               errno_item->valueint,
               cJSON_IsString(error_item) ? error_item->valuestring : "unknown");
      if (errno_item->valueint == 1) {
        ESP_LOGW(TAG, "Legacy OTA auth failed");
      } else if (errno_item->valueint == 26) {
        ESP_LOGW(TAG, "Legacy OTA device id rejected");
      }
      cJSON_Delete(root);
      return OTA_CHECK_PARSE_SERVER_ERROR;
    }
  }

  cJSON *code = cJSON_GetObjectItem(root, "code");
  if (cJSON_IsNumber(code) && code->valueint != 0 && code->valueint != 200) {
    cJSON *msg = cJSON_GetObjectItem(root, "msg");
    ESP_LOGW(TAG, "OTA check returned code=%d, msg=%s", code->valueint,
             cJSON_IsString(msg) ? msg->valuestring : "unknown");
    cJSON_Delete(root);
    return OTA_CHECK_PARSE_SERVER_ERROR;
  }

  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsObject(data)) {
    ESP_LOGW(TAG, "OTA check response succeeded but did not include task data");
    cJSON_Delete(root);
    return OTA_CHECK_PARSE_SERVER_ERROR;
  }

  cJSON *url_item = cJSON_GetObjectItem(data, "url");
  if (cJSON_IsString(url_item) && url_item->valuestring[0] != '\0') {
    snprintf(download_url, download_url_len, "%s", url_item->valuestring);
  } else {
    cJSON *token_item = cJSON_GetObjectItem(data, "token");
    cJSON *ip_port_item = cJSON_GetObjectItem(data, "ipPort");
    if (cJSON_IsString(token_item) && cJSON_IsString(ip_port_item)) {
      snprintf(download_token, download_token_len, "%s", token_item->valuestring);
      snprintf(download_url, download_url_len, "http://%s/ota/south/download/%s",
               ip_port_item->valuestring, token_item->valuestring);
    }
  }

  cJSON_Delete(root);

  if (download_url[0] == '\0') {
    ESP_LOGW(TAG, "OTA check succeeded but no download URL was returned");
    return OTA_CHECK_PARSE_SERVER_ERROR;
  }

  return OTA_CHECK_PARSE_HAS_TASK;
}
#endif

static void log_dns_server(const char *label,
                           const esp_netif_dns_info_t *dns_info) {
  if (dns_info == NULL) {
    return;
  }

  if (dns_info->ip.type == ESP_IPADDR_TYPE_V4) {
    ESP_LOGI(TAG, "%s " IPSTR, label, IP2STR(&dns_info->ip.u_addr.ip4));
    return;
  }

  ESP_LOGI(TAG, "%s type=%d", label, dns_info->ip.type);
}

static void log_ota_network_snapshot(const char *phase) {
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta == NULL) {
    ESP_LOGW(TAG, "[%s] WiFi STA netif is not available", phase);
    return;
  }

  esp_netif_t *default_netif = esp_netif_get_default_netif();
  if (default_netif != sta) {
    ESP_LOGW(TAG, "[%s] default netif is not WiFi STA", phase);
  }

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(sta, &ip_info) == ESP_OK) {
    ESP_LOGI(TAG, "[%s] STA IP=" IPSTR " GW=" IPSTR " MASK=" IPSTR, phase,
             IP2STR(&ip_info.ip), IP2STR(&ip_info.gw), IP2STR(&ip_info.netmask));
  } else {
    ESP_LOGW(TAG, "[%s] Failed to read STA IP info", phase);
  }

  esp_netif_dns_info_t dns_info;
  if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
    log_dns_server("[OTA] DNS main:", &dns_info);
  }
  if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK) {
    log_dns_server("[OTA] DNS backup:", &dns_info);
  }
  if (esp_netif_get_dns_info(sta, ESP_NETIF_DNS_FALLBACK, &dns_info) == ESP_OK) {
    log_dns_server("[OTA] DNS fallback:", &dns_info);
  }
}

/* Legacy OTA auth/version helpers removed from the active build. */
#if 0
static int decode_secret_or_raw(const char *secret, unsigned char *key_buf,
                                size_t key_buf_len) {
  if (secret == NULL || key_buf == NULL || key_buf_len == 0) {
    return 0;
  }

  memset(key_buf, 0, key_buf_len);
  if (looks_like_base64_secret(secret)) {
    size_t secret_len = strlen(secret);
    size_t padded_len = secret_len;
    while ((padded_len % 4) != 0) {
      ++padded_len;
    }

    if (padded_len < 192) {
      char normalized_secret[192] = {0};
      memcpy(normalized_secret, secret, secret_len);
      for (size_t i = secret_len; i < padded_len; ++i) {
        normalized_secret[i] = '=';
      }

      int key_len = base64_decode(normalized_secret, key_buf);
      if (key_len > 0 && (size_t)key_len <= key_buf_len) {
        if (padded_len != secret_len) {
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

static esp_err_t build_onenet_token(const char *resource, const char *secret,
                                    char *out, size_t out_len) {
  if (resource == NULL || secret == NULL || out == NULL || out_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  unsigned char key_buf[128];
  int key_len = decode_secret_or_raw(secret, key_buf, sizeof(key_buf));
  if (key_len <= 0) {
    return ESP_FAIL;
  }

  char string_for_signature[256];
  int ret = snprintf(string_for_signature, sizeof(string_for_signature),
                     "%s\nsha1\n%s\n2018-10-31", UNIX, resource);
  if (ret < 0 || ret >= (int)sizeof(string_for_signature)) {
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
  if (ret < 0 || ret >= (int)out_len) {
    return ESP_ERR_INVALID_SIZE;
  }

  return ESP_OK;
}

#endif

static esp_err_t read_http_body(esp_http_client_handle_t client, char **out_body,
                                size_t *out_len) {
  if (client == NULL || out_body == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  size_t capacity = 1024;
  size_t length = 0;
  char *body = malloc(capacity);
  if (body == NULL) {
    return ESP_ERR_NO_MEM;
  }

  while (1) {
    if (length + 512 + 1 > capacity) {
      capacity *= 2;
      char *new_body = realloc(body, capacity);
      if (new_body == NULL) {
        free(body);
        return ESP_ERR_NO_MEM;
      }
      body = new_body;
    }

    int read_len =
        esp_http_client_read(client, body + length, capacity - length - 1);
    if (read_len < 0) {
      free(body);
      return ESP_FAIL;
    }
    if (read_len == 0) {
      break;
    }
    length += (size_t)read_len;
  }

  body[length] = '\0';
  *out_body = body;
  if (out_len != NULL) {
    *out_len = length;
  }
  return ESP_OK;
}

static esp_err_t ota_http_get(const char *url, const char *authorization,
                              int timeout_ms, int *status_code, char **out_body,
                              size_t *out_len) {
  if (url == NULL || status_code == NULL || out_body == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *status_code = 0;
  *out_body = NULL;
  if (out_len != NULL) {
    *out_len = 0;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = timeout_ms,
      .keep_alive_enable = false,
      .addr_type = HTTP_ADDR_TYPE_INET,
  };
  if (ota_url_is_https(url)) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    return ESP_ERR_NO_MEM;
  }

  if (authorization != NULL && authorization[0] != '\0') {
    esp_http_client_set_header(client, "Authorization", authorization);
  }
  esp_http_client_set_header(client, "Accept", "application/json");

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
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
                                  char **out_body, size_t *out_len) {
  if (url == NULL || status_code == NULL || out_body == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  *status_code = 0;
  *out_body = NULL;
  if (out_len != NULL) {
    *out_len = 0;
  }

  esp_http_client_config_t config = {
      .url = url,
      .method = method,
      .timeout_ms = timeout_ms,
      .keep_alive_enable = false,
      .addr_type = HTTP_ADDR_TYPE_INET,
  };
  if (ota_url_is_https(url)) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    return ESP_ERR_NO_MEM;
  }

  if (authorization != NULL && authorization[0] != '\0') {
    esp_http_client_set_header(client, "Authorization", authorization);
  }
  esp_http_client_set_header(client, "Accept", "application/json");
  if (content_type != NULL && content_type[0] != '\0') {
    esp_http_client_set_header(client, "Content-Type", content_type);
  }

  int body_len = body != NULL ? (int)strlen(body) : 0;
  esp_err_t err = esp_http_client_open(client, body_len);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }

  if (body_len > 0) {
    int written = esp_http_client_write(client, body, body_len);
    if (written != body_len) {
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

#if 0
static esp_err_t ota_report_device_version(const char *authorization,
                                           const char *dev_id,
                                           const char *version) {
  if (authorization == NULL || authorization[0] == '\0' || dev_id == NULL ||
      dev_id[0] == '\0' || version == NULL || version[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  char url[256] = {0};
  char body[96] = {0};
  int ret = snprintf(url, sizeof(url),
                     "http://legacy-ota-removed.invalid/device/version?dev_id=%s",
                     dev_id);
  if (ret < 0 || ret >= (int)sizeof(url)) {
    return ESP_ERR_INVALID_SIZE;
  }
  ret = snprintf(body, sizeof(body), "{\"s_version\":\"%s\"}", version);
  if (ret < 0 || ret >= (int)sizeof(body)) {
    return ESP_ERR_INVALID_SIZE;
  }

  int status_code = 0;
  char *resp_body = NULL;
  size_t resp_len = 0;
  ESP_LOGI(TAG, "Reporting OTA device version: %s body=%s", url, body);
  esp_err_t err = ota_http_request(url, HTTP_METHOD_POST, authorization,
                                   "application/json", body, 4000, &status_code,
                                   &resp_body, &resp_len);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "OTA version report request failed: %s", esp_err_to_name(err));
    free(resp_body);
    return err;
  }

  ESP_LOGI(TAG, "OTA version report status=%d, body=%s", status_code,
           resp_body != NULL ? resp_body : "");
  if (status_code < 200 || status_code >= 300 || resp_body == NULL ||
      resp_len == 0) {
    free(resp_body);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_body);
  free(resp_body);
  if (root == NULL) {
    return ESP_FAIL;
  }

  cJSON *errno_item = cJSON_GetObjectItem(root, "errno");
  cJSON *error_item = cJSON_GetObjectItem(root, "error");
  esp_err_t result = ESP_OK;
  if (!cJSON_IsNumber(errno_item) || errno_item->valueint != 0) {
    ESP_LOGW(TAG, "OTA version report returned errno=%d, error=%s",
             cJSON_IsNumber(errno_item) ? errno_item->valueint : -1,
             cJSON_IsString(error_item) ? error_item->valuestring : "unknown");
    result = ESP_FAIL;
  }
  cJSON_Delete(root);
  return result;
}

#endif

typedef struct {
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
                                               int query_type) {
  if (body == NULL || task_info == NULL || has_task == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(task_info, 0, sizeof(*task_info));
  *has_task = false;

  cJSON *root = cJSON_Parse(body);
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to parse fuse-ota check response");
    return ESP_FAIL;
  }

  cJSON *code_item = cJSON_GetObjectItem(root, "code");
  cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
  if (!cJSON_IsNumber(code_item)) {
    ESP_LOGW(TAG, "fuse-ota check response did not include numeric code");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  if (code_item->valueint != 0) {
    ESP_LOGW(TAG, "fuse-ota check returned code=%d, msg=%s",
             code_item->valueint,
             cJSON_IsString(msg_item) ? msg_item->valuestring : "unknown");
    if (code_item->valueint == 12012) {
      ESP_LOGI(TAG,
               "No runnable fuse-ota task for fw=%s type=%d. Verify the OneNET task source version matches the device firmware version.",
               gateway_firmware_version(), query_type);
      ESP_LOGI(TAG,
               "当前没有匹配的 fuse-ota 任务: product=%s, device=%s, type=%d, "
               "version=%s。通常表示这台设备/这个版本当前没有待执行的升级任务，"
               "或者上一条任务已经被关闭。",
               GW_PRODUCTID, GW_DEVICENAME, query_type,
               gateway_firmware_version());
      cJSON_Delete(root);
      return ESP_OK;
    }
    if (code_item->valueint == 12010) {
      ESP_LOGW(TAG,
               "fuse-ota 任务类型不匹配：本次查询 type=%d，平台任务不是这个类型。"
               "将尝试另一种类型继续查询。",
               query_type);
      cJSON_Delete(root);
      return ESP_ERR_NOT_SUPPORTED;
    }
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  cJSON *data = cJSON_GetObjectItem(root, "data");
  if (!cJSON_IsObject(data)) {
    ESP_LOGI(TAG, "当前固件版本没有可执行的 OTA 任务");
    cJSON_Delete(root);
    return ESP_OK;
  }

  cJSON *tid_item = cJSON_GetObjectItem(data, "tid");
  if (!copy_json_string_or_number(tid_item, task_info->tid,
                                  sizeof(task_info->tid))) {
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
                                        const char *status_url, int step) {
  if (authorization == NULL || authorization[0] == '\0' || status_url == NULL ||
      status_url[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  char body[32] = {0};
  int ret = snprintf(body, sizeof(body), "{\"step\":%d}", step);
  if (ret < 0 || ret >= (int)sizeof(body)) {
    return ESP_ERR_INVALID_SIZE;
  }

  int status_code = 0;
  char *resp_body = NULL;
  size_t resp_len = 0;
  ESP_LOGI(TAG, "Reporting fuse-ota status step=%d: %s", step, status_url);
  esp_err_t err = ota_http_request(status_url, HTTP_METHOD_POST, authorization,
                                   "application/json", body, 4000, &status_code,
                                   &resp_body, &resp_len);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "fuse-ota status report failed: %s", esp_err_to_name(err));
    free(resp_body);
    return err;
  }

  ESP_LOGI(TAG, "fuse-ota status report status=%d, body=%s", status_code,
           resp_body != NULL ? resp_body : "");
  if (status_code < 200 || status_code >= 300 || resp_body == NULL ||
      resp_len == 0) {
    free(resp_body);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(resp_body);
  free(resp_body);
  if (root == NULL) {
    return ESP_FAIL;
  }

  cJSON *code_item = cJSON_GetObjectItem(root, "code");
  cJSON *msg_item = cJSON_GetObjectItem(root, "msg");
  esp_err_t result = ESP_OK;
  if (!cJSON_IsNumber(code_item) || code_item->valueint != 0) {
    ESP_LOGW(TAG, "fuse-ota status report returned code=%d, msg=%s",
             cJSON_IsNumber(code_item) ? code_item->valueint : -1,
             cJSON_IsString(msg_item) ? msg_item->valuestring : "unknown");
    result = ESP_FAIL;
  }
  cJSON_Delete(root);
  return result;
}

static int ota_map_fuse_failure_step(esp_err_t err) {
  if (err == ESP_ERR_NO_MEM) {
    return 103;
  }
  if (err == ESP_ERR_TIMEOUT) {
    return 104;
  }
  if (err == ESP_ERR_INVALID_CRC) {
    return 205;
  }
  return 107;
}

static void md5_digest_to_hex(const unsigned char digest[16], char *out_hex,
                              size_t out_hex_len) {
  static const char hex_chars[] = "0123456789abcdef";
  if (digest == NULL || out_hex == NULL || out_hex_len < 33) {
    return;
  }

  for (size_t i = 0; i < 16; ++i) {
    out_hex[i * 2] = hex_chars[(digest[i] >> 4) & 0x0F];
    out_hex[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
  }
  out_hex[32] = '\0';
}

static void ota_pending_info_clear(void) {
  ota_pending_info_t pending = {0};
  EEprom_WriteData(OTA_PENDING_INFO_KEY, &pending, sizeof(pending));
}

static void ota_pending_info_save(const char *status_url,
                                  const char *target_version) {
  ota_pending_info_t pending = {0};
  pending.magic = OTA_PENDING_INFO_MAGIC;
  snprintf(pending.status_url, sizeof(pending.status_url), "%s",
           status_url != NULL ? status_url : "");
  snprintf(pending.target_version, sizeof(pending.target_version), "%s",
           target_version != NULL ? target_version : "");
  EEprom_WriteData(OTA_PENDING_INFO_KEY, &pending, sizeof(pending));
}

static bool ota_pending_info_load(ota_pending_info_t *out_pending) {
  if (out_pending == NULL) {
    return false;
  }

  memset(out_pending, 0, sizeof(*out_pending));
  EEprom_ReadData(OTA_PENDING_INFO_KEY, out_pending, sizeof(*out_pending));
  return out_pending->magic == OTA_PENDING_INFO_MAGIC &&
         out_pending->status_url[0] != '\0';
}

static void ota_notify_reboot_request_clear(void) {
  ota_notify_reboot_info_t reboot_info = {0};
  EEprom_WriteData(OTA_NOTIFY_REBOOT_KEY, &reboot_info, sizeof(reboot_info));
}

static bool ota_notify_reboot_request_load(ota_notify_reboot_info_t *out_info) {
  if (out_info == NULL) {
    return false;
  }

  memset(out_info, 0, sizeof(*out_info));
  EEprom_ReadData(OTA_NOTIFY_REBOOT_KEY, out_info, sizeof(*out_info));
  return out_info->magic == OTA_NOTIFY_REBOOT_MAGIC;
}

void WiFi_Cat1_RequestOtaNotifyReboot(void) {
  ota_notify_reboot_info_t reboot_info = {
      .magic = OTA_NOTIFY_REBOOT_MAGIC,
  };
  EEprom_WriteData(OTA_NOTIFY_REBOOT_KEY, &reboot_info, sizeof(reboot_info));
}

bool WiFi_Cat1_BeginPendingOtaNotifyBootstrap(void) {
  ota_notify_reboot_info_t reboot_info = {0};
  if (!ota_notify_reboot_request_load(&reboot_info)) {
    return false;
  }

  ota_notify_reboot_request_clear();
  s_ota_notify_bootstrap_active = true;
  return true;
}

void WiFi_Cat1_FinishOtaNotifyBootstrap(void) {
  s_ota_notify_bootstrap_active = false;
}

bool WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive(void) {
  ota_notify_reboot_info_t reboot_info = {0};
  return s_ota_notify_bootstrap_active ||
         ota_notify_reboot_request_load(&reboot_info);
}

static esp_err_t perform_streaming_ota_once(const char *url, bool add_range,
                                            bool *out_zero_byte) {
  if (url == NULL || url[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }
  if (out_zero_byte != NULL) {
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
  if (ota_url_is_https(url)) {
    config.crt_bundle_attach = esp_crt_bundle_attach;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    return ESP_ERR_NO_MEM;
  }

  if (g_ota_download_auth[0] != '\0') {
    esp_http_client_set_header(client, "Authorization", g_ota_download_auth);
  }
  if (add_range) {
    esp_http_client_set_header(client, "Range", "0-");
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }

  int content_length = esp_http_client_fetch_headers(client);
  int status_code = esp_http_client_get_status_code(client);
  if (status_code != 200 && status_code != 206) {
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
      header_value != NULL) {
    ota_errno = header_value;
  }
  if (esp_http_client_get_header(client, "Content-Range", &header_value) == ESP_OK &&
      header_value != NULL) {
    content_range = header_value;
  }
  if (esp_http_client_get_header(client, "Transfer-Encoding", &header_value) ==
          ESP_OK &&
      header_value != NULL) {
    transfer_encoding = header_value;
  }
  if (esp_http_client_get_header(client, "Content-Type", &header_value) == ESP_OK &&
      header_value != NULL) {
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
  if (ota_errno != NULL && strcmp(ota_errno, "0") != 0) {
    ESP_LOGE(TAG, "OTA server rejected download, Ota-Errno=%s", ota_errno);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }
  if (content_length > 0 && expected_size > 0 && content_length != expected_size) {
    ESP_LOGW(TAG,
             "OTA size hint mismatch between HTTP header and task info: header=%d task=%d",
             content_length, expected_size);
  }

  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }

  /* Memory guard: require at least 64 KB internal + 128 KB SPIRAM free before OTA */
  {
    uint32_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t free_spi = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_int < 64 * 1024 || free_spi < 128 * 1024) {
      ESP_LOGE(TAG, "Insufficient heap for OTA: DRAM=%" PRIu32 " SPIRAM=%" PRIu32,
               free_int, free_spi);
      esp_http_client_cleanup(client);
      return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "OTA heap check passed: DRAM=%" PRIu32 " SPIRAM=%" PRIu32,
             free_int, free_spi);
  }

  esp_ota_handle_t ota_handle = 0;
  bool ota_started = false;
  err = esp_ota_begin(update_partition,
                      expected_size > 0 ? expected_size : OTA_SIZE_UNKNOWN,
                      &ota_handle);
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }
  ota_started = true;

  uint8_t *buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
  if (buffer == NULL) {
    buffer = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL);
  }
  if (buffer == NULL) {
    esp_ota_abort(ota_handle);
    esp_http_client_cleanup(client);
    return ESP_ERR_NO_MEM;
  }

  iot_md5_context md5_ctx;
  bool md5_enabled = (g_ota_expected_md5[0] != '\0');
  if (md5_enabled) {
    utils_md5_init(&md5_ctx);
    utils_md5_starts(&md5_ctx);
  }

  size_t total_written = 0;
  int read_iterations = 0;
  while (1) {
    int read_len = esp_http_client_read(client, (char *)buffer, 4096);
    if (read_len < 0) {
      err = ESP_FAIL;
      break;
    }
    if (read_len == 0) {
      ESP_LOGI(TAG, "OTA download stream reached EOF after %d reads, total=%u bytes",
               read_iterations, (unsigned int)total_written);
      err = ESP_OK;
      break;
    }
    ++read_iterations;
    if (read_iterations <= 3 || (read_iterations % 64) == 0 ||
        (total_written + (size_t)read_len) >= (size_t)expected_size) {
      ESP_LOGI(TAG, "OTA 下载分块 %d: 本次 %d 字节，累计 %u/%d 字节",
               read_iterations, read_len,
               (unsigned int)(total_written + (size_t)read_len), expected_size);
    }

    err = esp_ota_write(ota_handle, buffer, (size_t)read_len);
    if (err != ESP_OK) {
      break;
    }
    if (md5_enabled) {
      utils_md5_update(&md5_ctx, buffer, (size_t)read_len);
    }
    total_written += (size_t)read_len;
  }

  free(buffer);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    if (ota_started) {
      esp_ota_abort(ota_handle);
    }
    return err;
  }

  if (total_written == 0) {
    if (out_zero_byte != NULL) {
      *out_zero_byte = true;
    }
    ESP_LOGE(TAG,
             "OTA download returned zero bytes. Check Ota-Errno, task state, and "
             "whether the server expects Range-based download.");
    esp_ota_abort(ota_handle);
    return ESP_ERR_INVALID_SIZE;
  }

  if (expected_size > 0 && total_written != (size_t)expected_size) {
    ESP_LOGE(TAG, "OTA size mismatch: expected=%d actual=%u", expected_size,
             (unsigned int)total_written);
    esp_ota_abort(ota_handle);
    return ESP_ERR_INVALID_SIZE;
  }

  if (md5_enabled) {
    unsigned char digest[16];
    char digest_hex[33] = {0};
    utils_md5_finish(&md5_ctx, digest);
    utils_md5_free(&md5_ctx);
    md5_digest_to_hex(digest, digest_hex, sizeof(digest_hex));
    if (strcasecmp(digest_hex, g_ota_expected_md5) != 0) {
      ESP_LOGE(TAG, "OTA md5 mismatch: expected=%s actual=%s", g_ota_expected_md5,
               digest_hex);
      esp_ota_abort(ota_handle);
      return ESP_ERR_INVALID_CRC;
    }
    ESP_LOGI(TAG, "OTA md5 verified: %s", digest_hex);
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    return err;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    return err;
  }

  ESP_LOGI(TAG, "OTA image written successfully: %u bytes",
           (unsigned int)total_written);
  return ESP_OK;
}

static esp_err_t perform_streaming_ota(const char *url) {
  bool zero_byte = false;
  esp_err_t err = perform_streaming_ota_once(url, true, &zero_byte);
  if (err == ESP_OK || !zero_byte) {
    return err;
  }

  ESP_LOGW(TAG,
           "OTA download returned zero bytes with Range header, retrying without Range: %s",
           url);
  zero_byte = false;
  err = perform_streaming_ota_once(url, false, &zero_byte);
  if (err == ESP_OK || !zero_byte) {
    return err;
  }

  if (strncmp(url, "https://", strlen("https://")) == 0) {
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

#undef TAG
#define TAG WIFI_CAT1_TAG

/**
 * @brief 子设备上线/下线报备 (按照新版 OneNET 规范修正)
 * 格式要求：params 下嵌套 subDevices 数组
 */
esp_err_t WiFi_Cat1_SubOnline(char sub_num, char mode) {
  esp_err_t err = ESP_FAIL;
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return ESP_ERR_NO_MEM;

  // 使用简单的 13 位以内 ID
  char request_id[32];
  snprintf(request_id, sizeof(request_id), "%llu",
           (unsigned long long)(esp_timer_get_time() / 1000ULL));
  cJSON_AddStringToObject(root, "id", request_id);
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 关键修改：直接添加到 params 下，不要建数组！
  cJSON_AddStringToObject(params, "productID", SUB_PRODUCTID);
  cJSON_AddStringToObject(params, "deviceName", DeviceNameBuff[(int)sub_num]);

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    if (mode == 0) {
      snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/logout",
               GW_PRODUCTID, GW_DEVICENAME);
    } else {
      snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/login",
               GW_PRODUCTID, GW_DEVICENAME);
    }

    err = Cat1_AT_MqttPublish(temptopic, post_data);
    if (err == ESP_OK) {
      ESP_LOGI(TAG, "SubOnline sent (Direct Params Format): %s", post_data);
    } else {
      ESP_LOGW(TAG, "SubOnline publish failed, topic=%s, err=%s", temptopic,
               esp_err_to_name(err));
    }
    free(post_data);
  } else {
    err = ESP_ERR_NO_MEM;
  }

  cJSON_Delete(root);
  return err;
}

/**
 * @brief 网关自身数据上报 (使用普通的 property/post)
 */
void WiFi_Cat1_GatewayDataPost(float temp, float hum, float lux) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以内 ID
  cJSON_AddStringToObject(root, "id", "10804");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 终极精度修复：使用 cJSON_CreateRaw 强制保留两位小数，消灭裸整数
  char t_str[16], h_str[16], l_str[16];
  snprintf(t_str, sizeof(t_str), "%.2f", (double)temp);
  snprintf(h_str, sizeof(h_str), "%.2f", (double)hum);
  snprintf(l_str, sizeof(l_str), "%.2f", (double)lux);

  // 1. 空气温度
  cJSON *temp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE5);
  cJSON_AddItemToObject(temp_obj, "value", cJSON_CreateRaw(t_str));

  // 2. 空气湿度
  cJSON *hum_obj = cJSON_AddObjectToObject(params, ATTRIBUTE6);
  cJSON_AddItemToObject(hum_obj, "value", cJSON_CreateRaw(h_str));

  // 3. 光照强度
  if (lux >= 0.0f) {
    cJSON *lux_obj = cJSON_AddObjectToObject(params, ATTRIBUTE7);
    cJSON_AddItemToObject(lux_obj, "value", cJSON_CreateRaw(l_str));
  }

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "GatewayDataPost 已发送: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

/**
 * @brief 子设备代理上报 (按照新版 OneNET Studio pack/post 规范重构)
 * 关键点：
 * 1. 使用 pack/post 主题
 * 2. params 为数组，每个元素包含 identity (PID/SN) 和 properties
 * 3. properties 内每个属性必须嵌套 {"value": xxx}
 */
void WiFi_Cat1_NodeDataPost(float temp, float hum, float lux) {
  // 关键保护逻辑：仅在 MQTT 已连接 且 LoRa 已确认通信 且
  // 子设备尚未报备上线时，才执行上线报备
  if ((SysCB.SysEventFlag & (CONNECT_WIFI | CONNECT_CAT1)) &&
      (SysCB.SysEventFlag & CONNECT_MQTT) &&
      (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
      !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
    ESP_LOGW(TAG, "LoRa 通信已确认，正在向 OneNET 报备子设备上线...");
    if (WiFi_Cat1_SubOnline(1, 1) == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    return;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 1. 基础字段
  cJSON_AddStringToObject(root, "id", "123456");
  cJSON_AddStringToObject(root, "version", "1.0");

  // 2. params 为数组 (pack 接口核心)
  cJSON *params_array = cJSON_AddArrayToObject(root, "params");
  cJSON *sub_obj = cJSON_CreateObject();
  cJSON_AddItemToArray(params_array, sub_obj);

  // 3. identity 嵌套回归
  cJSON *identity = cJSON_AddObjectToObject(sub_obj, "identity");
  cJSON_AddStringToObject(identity, "productID", SUB_PRODUCTID);
  cJSON_AddStringToObject(identity, "deviceName", DeviceNameBuff[1]);

  // 4. properties 嵌套
  cJSON *properties = cJSON_AddObjectToObject(sub_obj, "properties");

  // 5. 格式化数值并执行 {"value": xxx} 嵌套
  char nt_str[16], nh_str[16], nl_str[16];
  snprintf(nt_str, sizeof(nt_str), "%.2f", (double)temp);
  snprintf(nh_str, sizeof(nh_str), "%.2f", (double)hum);
  snprintf(nl_str, sizeof(nl_str), "%.2f", (double)lux);

  // 温度
  cJSON *t_obj = cJSON_AddObjectToObject(properties, ATTRIBUTE_TEMP);
  cJSON_AddItemToObject(t_obj, "value", cJSON_CreateRaw(nt_str));

  // 湿度
  cJSON *h_obj = cJSON_AddObjectToObject(properties, ATTRIBUTE_HUMI);
  cJSON_AddItemToObject(h_obj, "value", cJSON_CreateRaw(nh_str));

  // 光照
  cJSON *l_obj = cJSON_AddObjectToObject(properties, ATTRIBUTE_LIGHTLUX);
  cJSON_AddItemToObject(l_obj, "value", cJSON_CreateRaw(nl_str));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    // 6. 切换到 pack/post 主题
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/pack/post",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "NodeDataPost (Pack/Post Spec) 已发送: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_SoilDataPost(float temp, float humi, float ec, float n, float p,
                            float k) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以内 ID
  cJSON_AddStringToObject(root, "id", "12906");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 终极精度修复：使用 cJSON_CreateRaw 强制保留两位小数，消灭裸整数
  char st_str[16], sh_str[16], se_str[16], sn_str[16], sp_str[16], sk_str[16];
  snprintf(st_str, sizeof(st_str), "%.2f", (double)temp);
  snprintf(sh_str, sizeof(sh_str), "%.2f", (double)humi);
  snprintf(se_str, sizeof(se_str), "%.2f", (double)ec);
  snprintf(sn_str, sizeof(sn_str), "%.2f", (double)n);
  snprintf(sp_str, sizeof(sp_str), "%.2f", (double)p);
  snprintf(sk_str, sizeof(sk_str), "%.2f", (double)k);

  // 温度 (double)
  cJSON *temp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_TEMP);
  cJSON_AddItemToObject(temp_obj, "value", cJSON_CreateRaw(st_str));

  // 水分 (double)
  cJSON *humi_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_HUMI);
  cJSON_AddItemToObject(humi_obj, "value", cJSON_CreateRaw(sh_str));

  // EC (double)
  cJSON *ec_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_EC);
  cJSON_AddItemToObject(ec_obj, "value", cJSON_CreateRaw(se_str));

  // 氮 (double)
  cJSON *n_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_N);
  cJSON_AddItemToObject(n_obj, "value", cJSON_CreateRaw(sn_str));

  // 磷 (double)
  cJSON *p_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_P);
  cJSON_AddItemToObject(p_obj, "value", cJSON_CreateRaw(sp_str));

  // 钾 (double)
  cJSON *k_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_K);
  cJSON_AddItemToObject(k_obj, "value", cJSON_CreateRaw(sk_str));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "SoilDataPost sent to %s: %s", temptopic, post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_AdcDataPost(float adc1, float adc2, float adc3) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以内 ID
  cJSON_AddStringToObject(root, "id", "3092");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 终极精度修复：使用 cJSON_CreateRaw 强制保留两位小数
  char a1_str[16], a2_str[16], a3_str[16];
  snprintf(a1_str, sizeof(a1_str), "%.2f", (double)adc1);
  snprintf(a2_str, sizeof(a2_str), "%.2f", (double)adc2);
  snprintf(a3_str, sizeof(a3_str), "%.2f", (double)adc3);

  // ADC1
  cJSON *adc1_obj = cJSON_AddObjectToObject(params, ATTRIBUTE8);
  cJSON_AddItemToObject(adc1_obj, "value", cJSON_CreateRaw(a1_str));

  // ADC2
  cJSON *adc2_obj = cJSON_AddObjectToObject(params, ATTRIBUTE9);
  cJSON_AddItemToObject(adc2_obj, "value", cJSON_CreateRaw(a2_str));

  // ADC3
  cJSON *adc3_obj = cJSON_AddObjectToObject(params, ATTRIBUTE10);
  cJSON_AddItemToObject(adc3_obj, "value", cJSON_CreateRaw(a3_str));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "AdcDataPost sent to %s: %s", temptopic, post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_AllDataPost(float air_temp, float air_hum, float air_lux,
                           float soil_temp, float soil_humi, float soil_ec,
                           float soil_n, float soil_p, float soil_k, float adc1,
                           float adc2, float adc3) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以内 ID
  cJSON_AddStringToObject(root, "id", "5011");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 终极精度修复：使用 cJSON_CreateRaw 强制转换两位小数
  char at_str[16], ah_str[16], al_str[16];
  snprintf(at_str, sizeof(at_str), "%.2f", (double)air_temp);
  snprintf(ah_str, sizeof(ah_str), "%.2f", (double)air_hum);
  snprintf(al_str, sizeof(al_str), "%.2f", (double)air_lux);

  char st_str[16], sh_str[16], se_str[16], sn_str[16], sp_str[16], sk_str[16];
  snprintf(st_str, sizeof(st_str), "%.2f", (double)soil_temp);
  snprintf(sh_str, sizeof(sh_str), "%.2f", (double)soil_humi);
  snprintf(se_str, sizeof(se_str), "%.2f", (double)soil_ec);
  snprintf(sn_str, sizeof(sn_str), "%.2f", (double)soil_n);
  snprintf(sp_str, sizeof(sp_str), "%.2f", (double)soil_p);
  snprintf(sk_str, sizeof(sk_str), "%.2f", (double)soil_k);

  // 1. 网关空气温湿度 + 光强
  cJSON *temp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE5);
  cJSON_AddItemToObject(temp_obj, "value", cJSON_CreateRaw(at_str));
  cJSON *hum_obj = cJSON_AddObjectToObject(params, ATTRIBUTE6);
  cJSON_AddItemToObject(hum_obj, "value", cJSON_CreateRaw(ah_str));
  cJSON *lux_obj = cJSON_AddObjectToObject(params, ATTRIBUTE7);
  cJSON_AddItemToObject(lux_obj, "value", cJSON_CreateRaw(al_str));

  // 2. 土壤传感器数据
  cJSON *stemp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_TEMP);
  cJSON_AddItemToObject(stemp_obj, "value", cJSON_CreateRaw(st_str));
  cJSON *shum_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_HUMI);
  cJSON_AddItemToObject(shum_obj, "value", cJSON_CreateRaw(sh_str));
  cJSON *sec_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_EC);
  cJSON_AddItemToObject(sec_obj, "value", cJSON_CreateRaw(se_str));
  cJSON *sn_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_N);
  cJSON_AddItemToObject(sn_obj, "value", cJSON_CreateRaw(sn_str));
  cJSON *sp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_P);
  cJSON_AddItemToObject(sp_obj, "value", cJSON_CreateRaw(sp_str));
  cJSON *sk_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_K);
  cJSON_AddItemToObject(sk_obj, "value", cJSON_CreateRaw(sk_str));

  // 3. ADC 数据
  char a1_str[16], a2_str[16], a3_str[16];
  snprintf(a1_str, sizeof(a1_str), "%.2f", (double)adc1);
  snprintf(a2_str, sizeof(a2_str), "%.2f", (double)adc2);
  snprintf(a3_str, sizeof(a3_str), "%.2f", (double)adc3);

  cJSON *adc1_obj = cJSON_AddObjectToObject(params, ATTRIBUTE8);
  cJSON_AddItemToObject(adc1_obj, "value", cJSON_CreateRaw(a1_str));
  cJSON *adc2_obj = cJSON_AddObjectToObject(params, ATTRIBUTE9);
  cJSON_AddItemToObject(adc2_obj, "value", cJSON_CreateRaw(a2_str));
  cJSON *adc3_obj = cJSON_AddObjectToObject(params, ATTRIBUTE10);
  cJSON_AddItemToObject(adc3_obj, "value", cJSON_CreateRaw(a3_str));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "AllDataPost (合并上报) 已发送: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

/**
 * @brief 主动获取子设备属性 (按照 OneNET 规范)
 * @param sub_num 子设备索引 (对应 DeviceNameBuff)
 */
void WiFi_Cat1_SubPropertyGet(char sub_num) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以内 ID
  cJSON_AddStringToObject(root, "id", "7503");
  cJSON_AddStringToObject(root, "version", "1.0");

  cJSON *params = cJSON_AddObjectToObject(root, "params");
  cJSON_AddStringToObject(params, "deviceName", DeviceNameBuff[(int)sub_num]);
  cJSON_AddStringToObject(params, "productID", SUB_PRODUCTID);

  cJSON *attr_list = cJSON_AddArrayToObject(params, "params");
  cJSON_AddItemToArray(attr_list, cJSON_CreateString(ATTRIBUTE_TEMP));
  cJSON_AddItemToArray(attr_list, cJSON_CreateString(ATTRIBUTE_HUMI));
  cJSON_AddItemToArray(attr_list, cJSON_CreateString(ATTRIBUTE_LIGHTLUX));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/property/get",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "SubPropertyGet sent: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_InitGPIO(void) {
  if (CAT1_POWER_STATE_PIN >= 0) {
    gpio_set_direction(CAT1_POWER_STATE_PIN, GPIO_MODE_OUTPUT);
  }
  if (CAT1_POWER_STA_PIN >= 0) {
    gpio_set_direction(CAT1_POWER_STA_PIN, GPIO_MODE_INPUT);
  }
  if (CAT1_NET_STA_PIN >= 0) {
    gpio_set_direction(CAT1_NET_STA_PIN, GPIO_MODE_INPUT);
  }
}

void Cat1_Reset(void) {
  if (CAT1_POWER_STA == 1) { // 如果目前处于关机状态，进入该分支
    ESP_LOGI(
        TAG,
        "\r\n目前4G Cat1模块处于关机状态，准备开机\r\n"); // 串口输出信息 //
                                                          // 串口输出信息
    CAT1_POWER(1);                   // 拉高
    vTaskDelay(pdMS_TO_TICKS(1500)); // 延时
    CAT1_POWER(0);                   // 开机成功了，拉低
  } else { // 反之表示目前处于开机状态，进入该分支
    ESP_LOGI(TAG,
             "\r\n目前4G Cat1模块处于开机状态，准备重启\r\n"); // 串口输出信息
    CAT1_POWER(1);                                             // 先拉高
    vTaskDelay(pdMS_TO_TICKS(1500));                           // 延时
    CAT1_POWER(0);                               // 关机成功了，拉低
    ESP_LOGI(TAG, "\r\n关机成功，准备开机\r\n"); // 串口输出信息
    vTaskDelay(pdMS_TO_TICKS(6000));             // 延时
    CAT1_POWER(1);                               // 拉高
    vTaskDelay(pdMS_TO_TICKS(1500));             // 延时
    CAT1_POWER(0);                               // 开机成功了，拉低
  }
  ESP_LOGI(TAG,
           "开机成功，请等待4G Cat1模块注册上网络... ...\r\n"); // 串口输出信息
}

#undef TAG
#define TAG OTA_TAG

#if 0
/* Legacy OTA path kept only as a historical reference. */
void Legacy_OTA_CheckTask_Removed(void) {
  extern char Mqtt_Password[]; // 复用 MQTT 连接的 Token 进行鉴权

  char url[512];
  snprintf(url, sizeof(url),
           "https://legacy-ota-removed.invalid/check?product_id=%s&"
           "device_name=%s&version=%s",
           GW_PRODUCTID, GW_DEVICENAME, CURRENT_FW_VERSION);

  ESP_LOGI(TAG, "正在请求 Studio OTA 接口: %s", url);

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
      char *buffer = heap_caps_malloc(content_length + 1, MALLOC_CAP_SPIRAM);
      if (buffer == NULL) {
        buffer = heap_caps_malloc(content_length + 1, MALLOC_CAP_INTERNAL);
      }
      if (buffer) {
        int read_len = esp_http_client_read(client, buffer, content_length);
        if (read_len > 0) {
          buffer[read_len] = '\0';
          ESP_LOGI(TAG, "Studio OTA 返回数据: %s", buffer);

          cJSON *root = cJSON_Parse(buffer);
          if (root) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data && cJSON_IsObject(data)) {
              cJSON *url_item = cJSON_GetObjectItem(data, "url");

              if (url_item && cJSON_IsString(url_item)) {
                const char *download_url = url_item->valuestring;
                ESP_LOGI(TAG, ">>> 成功获取固件下载链接: %s", download_url);
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

void OneNET_FuseOTA_CheckTask(void) {
  extern char Mqtt_Password[];

  typedef struct {
    char url[512];
    char encoded_device_name[256];
    char download_url[512];
    char status_url[512];
    fuse_ota_task_info_t task_info;
  } ota_check_ctx_t;

  ESP_LOGI(TAG,
           "OTA check preconditions: CONNECT_WIFI=%d CONNECT_MQTT=%d OTA_RUNNING=%d token_ready=%d fw=%s",
           (SysCB.SysEventFlag & CONNECT_WIFI) ? 1 : 0,
           (SysCB.SysEventFlag & CONNECT_MQTT) ? 1 : 0,
           (SysCB.SysEventFlag & OTA_RUNNING) ? 1 : 0,
           Mqtt_Password[0] != '\0' ? 1 : 0, gateway_firmware_version());

  if (!(SysCB.SysEventFlag & (CONNECT_WIFI | CONNECT_CAT1))) {
    ESP_LOGW(TAG, "Skip OTA check because no network (WiFi/CAT1) is connected");
    return;
  }
  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGW(TAG, "Skip OTA check because MQTT is not connected yet");
    return;
  }
  if (SysCB.SysEventFlag & OTA_RUNNING) {
    ESP_LOGW(TAG, "Skip OTA check because OTA is already running");
    return;
  }
  if (Mqtt_Password[0] == '\0') {
    ESP_LOGE(TAG, "MQTT device token is empty, cannot call fuse-ota APIs");
    return;
  }

  static ota_check_ctx_t ota_ctx_storage;
  ota_check_ctx_t *ctx = &ota_ctx_storage;
  memset(ctx, 0, sizeof(*ctx));

  URL_encode((char *)GW_DEVICENAME, strlen(GW_DEVICENAME), ctx->encoded_device_name);

  ESP_LOGI(TAG, "开始检查 OTA 任务，当前 firmware_version=%s",
           gateway_firmware_version());
  ESP_LOGI(TAG, "OneNET_FuseOTA_CheckTask stack watermark=%u",
           (unsigned int)uxTaskGetStackHighWaterMark(NULL));

  char *buffer = NULL;
  size_t response_len = 0;
  int status_code = 0;
  esp_err_t err = ESP_FAIL;
  bool has_task = false;
  const int request_timeout_ms = 4000;
  int query_types[2] = {FUSE_OTA_TASK_TYPE, FUSE_OTA_TASK_TYPE == 1 ? 2 : 1};
  size_t query_type_count =
      query_types[0] == query_types[1] ? 1U : 2U;

  for (size_t type_idx = 0; type_idx < query_type_count; ++type_idx) {
    int query_type = query_types[type_idx];
    bool type_mismatch_seen = false;

    for (size_t i = 0;
         i < sizeof(g_fuse_ota_check_url_templates) /
                 sizeof(g_fuse_ota_check_url_templates[0]);
         ++i) {
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
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "fuse-ota check request failed on endpoint %u: %s",
                 (unsigned int)(i + 1), esp_err_to_name(err));
        continue;
      }

      ESP_LOGI(TAG, "fuse-ota endpoint %u status=%d, body=%s",
               (unsigned int)(i + 1), status_code, buffer != NULL ? buffer : "");
      if (status_code < 200 || status_code >= 300 || buffer == NULL ||
          response_len == 0) {
        continue;
      }

      err = ota_parse_fuse_check_response(buffer, &ctx->task_info, &has_task,
                                          query_type);
      if (err == ESP_ERR_NOT_SUPPORTED) {
        type_mismatch_seen = true;
        continue;
      }
      if (err == ESP_OK) {
        break;
      }
    }

    if (err == ESP_OK) {
      break;
    }

    if (type_mismatch_seen && (type_idx + 1U) < query_type_count) {
      ESP_LOGW(TAG,
               "fuse-ota 查询 type=%d 未匹配平台任务，继续尝试 type=%d",
               query_type, query_types[type_idx + 1U]);
    }
  }

  free(buffer);
  buffer = NULL;

  if (err != ESP_OK || status_code < 200 || status_code >= 300) {
    ESP_LOGW(TAG, "所有 fuse-ota 检查接口都没有返回可用结果");
    goto cleanup;
  }
  if (!has_task) {
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

  if (ctx->task_info.task_status == 3) {
    if (ctx->task_info.target_version[0] != '\0' &&
        strcmp(ctx->task_info.target_version, gateway_firmware_version()) != 0) {
      esp_err_t report_err = ESP_FAIL;
      ESP_LOGW(TAG,
               "fuse-ota task is already in upgrading state, but current "
               "firmware_version=%s does not match target=%s. The old image "
               "should close this stale task with step=206 instead of "
               "pretending the upgrade succeeded.",
               gateway_firmware_version(), ctx->task_info.target_version);
      report_err = ota_report_fuse_status(Mqtt_Password, ctx->status_url, 206);
      if (report_err == ESP_OK) {
        ota_pending_info_clear();
      }
    } else {
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
  ESP_LOGI(TAG, "Launching gateway OTA download now");
  WiFi_Cat1_StartOTA(ctx->download_url, Mqtt_Password, 0);

cleanup:
  /* ctx is static, no free needed */
}

void OTAServer_process(uint8_t *data, uint16_t datalen, uint32_t page_index,
                       uint8_t is_last, uint8_t ota_staflag) {
  OTA_ZC_Chunk *new_chunk = ota_zc_pool_acquire(datalen);
  if (new_chunk != NULL) {
    new_chunk->page_index = page_index;
    new_chunk->is_last = is_last;
    new_chunk->ota_staflag = ota_staflag;
    memcpy(new_chunk->data, data, datalen);
    if (xQueueSend(OTA_ZC_Queue, new_chunk,
                   pdMS_TO_TICKS(OTA_ZC_SEND_TIMEOUT_MS)) != pdTRUE) {
      ota_zc_pool_release(new_chunk);
      g_ota_zc_stats.enqueue_fail++;
    } else {
      g_ota_zc_stats.enqueued++;
    }
  }
}

#if 0
void WiFi_Cat1_PropertyVersion(uint8_t num) {
  // 不再上报子设备版本信息
  if (num != 0) {
    return;
  }

  char versionatabuff[128]; // 临时缓冲区
  char tempdatabuff[512];   // 临时缓冲区 (加大以防止溢出)

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
    ESP_LOGI(TAG, "WiFi 模式下版本报备: %s", tempdatabuff);
  } else if (SysCB.SysEventFlag & CONNECT_CAT1) {
    bsp_uart_cat1_send(tempdatabuff, strlen(tempdatabuff));
  }
}

#endif

void WiFi_Cat1_PropertyVersion(uint8_t num) {
  if (num != 0) {
    return;
  }

  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGW(TAG, "MQTT 未连接，跳过 firmware_version 上报");
    return;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return;
  }

  cJSON_AddStringToObject(root, "id", "gw_fw_ver");
  cJSON_AddStringToObject(root, "version", "1.0");

  cJSON *params = cJSON_AddObjectToObject(root, "params");
  cJSON *fw_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_FIRMWARE_VER);
  cJSON_AddStringToObject(fw_obj, "value", gateway_firmware_version());

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data != NULL) {
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    if (Cat1_AT_MqttPublish(topic, post_data) == ESP_OK) {
      ESP_LOGI(TAG, "已上报 firmware_version=%s",
               gateway_firmware_version());
    } else {
      ESP_LOGE(TAG, "上报 firmware_version 失败");
    }
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_ReportBootOtaResult(void) {
  extern char Mqtt_Password[];

  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    return;
  }
  if (Mqtt_Password[0] == '\0') {
    ESP_LOGW(TAG, "MQTT 鉴权 token 为空，跳过 OTA 成功上报");
    return;
  }

  ota_pending_info_t pending;
  if (!ota_pending_info_load(&pending)) {
    return;
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running != NULL) {
    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
      if (mark_err == ESP_OK) {
        ESP_LOGI(TAG, "重启成功，已将 OTA 镜像标记为有效");
      } else {
        ESP_LOGW(TAG, "标记 OTA 镜像为有效失败: %s",
                 esp_err_to_name(mark_err));
        return;
      }
    }
  }

  if (pending.target_version[0] != '\0' &&
      strcmp(pending.target_version, gateway_firmware_version()) != 0) {
    ESP_LOGW(TAG,
             "待确认 OTA 目标版本=%s，但当前 firmware_version=%s；暂不上报 201",
             pending.target_version, gateway_firmware_version());
    return;
  }

  esp_err_t err =
      ota_report_fuse_status(Mqtt_Password, pending.status_url, 201);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "已上报 OTA 升级成功，firmware_version=%s",
             gateway_firmware_version());
    ota_pending_info_clear();
  } else {
    ESP_LOGW(TAG, "上报 OTA 升级成功失败，下次启动时继续重试");
  }
}

void WiFi_Cat1_StartOTA(const char *url, const char *token,
                        uint8_t ota_staflag) {
  if (url == NULL || url[0] == '\0') {
    ESP_LOGE(TAG, "OTA 下载地址为空");
    return;
  }

  if (ota_staflag != 0) {
    ESP_LOGW(TAG, "当前 ESP-IDF OTA 流程暂不支持子设备 OTA");
    return;
  }

  if (SysCB.SysEventFlag & OTA_RUNNING) {
    ESP_LOGW(TAG, "OTA 已在进行中，忽略重复请求");
    return;
  }

  snprintf(g_ota_download_auth, sizeof(g_ota_download_auth), "%s",
           token != NULL ? token : "");

  SysCB.SysEventFlag |= OTA_RUNNING;
  SysCB.SysEventFlag |= CONNECT_OTA;
  ESP_LOGI(TAG, "开始网关 OTA 下载: %s", url);
  log_ota_network_snapshot("before ota download");

  esp_err_t err = perform_streaming_ota(url);
  if (err != ESP_OK) {
    if (g_ota_status_url[0] != '\0' && g_ota_download_auth[0] != '\0') {
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

  if (g_ota_status_url[0] != '\0' && g_ota_download_auth[0] != '\0') {
    ota_report_fuse_status(g_ota_download_auth, g_ota_status_url, 100);
    ota_pending_info_save(g_ota_status_url, g_ota_target_version);
  }
  g_ota_download_auth[0] = '\0';
  g_ota_status_url[0] = '\0';
  g_ota_target_version[0] = '\0';
  g_ota_expected_size = 0;
  g_ota_expected_md5[0] = '\0';
  SysCB.SysEventFlag &= ~CONNECT_OTA;

  ESP_LOGI(TAG, "网关 OTA 已完成，准备重启进入新固件...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

/* OTA ZC consumer: dequeue chunk, esp_ota_write, return to pool */
void WiFi_Cat1_OTADownload(uint16_t a, uint16_t b, uint8_t c)
{
  (void)c;
  uint32_t total_size = (uint32_t)a;
  uint8_t  ota_staflag = (uint8_t)b;

  if (OTA_ZC_Queue == NULL) {
    ESP_LOGE(OTA_TAG, "OTA_ZC_Queue not initialized");
    return;
  }
  if (SysCB.SysEventFlag & OTA_RUNNING) {
    ESP_LOGW(OTA_TAG, "OTA_ZC consumer already running");
    return;
  }

  SysCB.SysEventFlag |= OTA_RUNNING;
  ota_zc_pool_init();

  const esp_partition_t *update_partition =
      esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(OTA_TAG, "OTA_ZC: no update partition");
    goto zc_cleanup;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition,
                                 total_size > 0 ? total_size : OTA_SIZE_UNKNOWN,
                                 &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(OTA_TAG, "OTA_ZC: esp_ota_begin failed: %s", esp_err_to_name(err));
    goto zc_cleanup;
  }

  ESP_LOGI(OTA_TAG, "OTA_ZC consumer started, waiting for chunks...");

  uint32_t total_written = 0;
  bool ota_done = false;
  while (!ota_done) {
    OTA_ZC_Chunk *chunk = NULL;
    if (xQueueReceive(OTA_ZC_Queue, &chunk, pdMS_TO_TICKS(30000)) != pdTRUE) {
      ESP_LOGW(OTA_TAG, "OTA_ZC: queue timeout after %u bytes",
               (unsigned int)total_written);
      break;
    }
    if (chunk == NULL) continue;
    if (chunk->ota_staflag != ota_staflag) {
      ota_zc_pool_release(chunk);
      continue;
    }
    g_ota_zc_stats.processed++;
    esp_err_t write_err = ESP_FAIL;
    for (int retry = 0; retry <= OTA_ZC_WRITE_RETRY_MAX; retry++) {
      write_err = esp_ota_write(ota_handle, chunk->data, (size_t)chunk->len);
      if (write_err == ESP_OK) break;
      if (retry < OTA_ZC_WRITE_RETRY_MAX) {
        g_ota_zc_stats.write_retry++;
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
    if (write_err != ESP_OK) {
      g_ota_zc_stats.write_fail++;
      ESP_LOGE(OTA_TAG, "OTA_ZC: write page=%u len=%u failed",
               (unsigned int)chunk->page_index, chunk->len);
      ota_zc_pool_release(chunk);
      break;
    }
    total_written += chunk->len;
    if (chunk->is_last) {
      ESP_LOGI(OTA_TAG, "OTA_ZC: last chunk, total=%u bytes",
               (unsigned int)total_written);
      ota_done = true;
    }
    ota_zc_pool_release(chunk);
  }

  if (ota_done && total_written > 0) {
    err = esp_ota_end(ota_handle);
    if (err == ESP_OK) {
      err = esp_ota_set_boot_partition(update_partition);
      if (err == ESP_OK) {
        ESP_LOGI(OTA_TAG, "OTA_ZC complete: %u bytes, restarting...",
                 (unsigned int)total_written);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
      }
    }
    if (err != ESP_OK) {
      ESP_LOGE(OTA_TAG, "OTA_ZC: end/set_boot failed: %s", esp_err_to_name(err));
    }
  } else {
    esp_ota_abort(ota_handle);
    ESP_LOGE(OTA_TAG, "OTA_ZC: download aborted");
  }

zc_cleanup:
  ota_zc_pool_deinit();
  SysCB.SysEventFlag &= ~OTA_RUNNING;
}


#undef TAG
#define TAG WIFI_CAT1_TAG

static esp_err_t Cat1_Send_AT_Command(const char *cmd, uint32_t timeout_ms,
                                      const char *expected_resp) {
  bsp_uart_cat1_send(cmd, strlen(cmd));
  uint32_t start_time = xTaskGetTickCount();
  if (g_at_rx_mutex) {
    xSemaphoreTake(g_at_rx_mutex, portMAX_DELAY);
    memset(g_at_rx_buffer, 0, sizeof(g_at_rx_buffer));
    g_at_data_ready = false;
    xSemaphoreGive(g_at_rx_mutex);
  }
  while ((xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS < timeout_ms) {
    if (g_at_data_ready) {
      xSemaphoreTake(g_at_rx_mutex, portMAX_DELAY);
      if (strstr(g_at_rx_buffer, "RDY")) {
        xSemaphoreGive(g_at_rx_mutex);
        return ESP_FAIL;
      }
      if (expected_resp && strstr(g_at_rx_buffer, expected_resp)) {
        xSemaphoreGive(g_at_rx_mutex);
        return ESP_OK;
      }
      xSemaphoreGive(g_at_rx_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return ESP_ERR_TIMEOUT;
}

void Cat1_AT_Mqtt_Task(void *pvParameters) {
  static char at_cmd[1024];
  ESP_LOGI(TAG, "Cat1 MQTT 监控任务已启动 (带启动保护延时)...");
  vTaskDelay(pdMS_TO_TICKS(10000));
  for (;;) {
    if (SysCB.SysEventFlag & CONNECT_WIFI) {
      vTaskDelay(pdMS_TO_TICKS(30000));
      continue;
    }
    if (SysCB.SysEventFlag & CONNECT_MQTT) {
      if (Cat1_Send_AT_Command("AT\r\n", 1000, "OK") != ESP_OK) {
        SysCB.SysEventFlag &= ~CONNECT_MQTT;
      }
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }
    int retry_main = 0;
    const int max_retry_main = 5;
    while (retry_main < max_retry_main) {
      Cat1_Send_AT_Command("AT+QIDEACT=1\r\n", 3000, "OK");
      vTaskDelay(pdMS_TO_TICKS(2000));
      if (Cat1_Send_AT_Command("AT+CPIN?\r\n", 5000, "+CPIN: READY") != ESP_OK)
        goto retry_init;
      vTaskDelay(pdMS_TO_TICKS(1000));
      bool registered = false;
      for (int i = 0; i < 15; i++) {
        if (Cat1_Send_AT_Command("AT+CGREG?\r\n", 2000, "+CGREG: 0,1") ==
                ESP_OK ||
            Cat1_Send_AT_Command("AT+CGREG?\r\n", 2000, "+CGREG: 0,5") ==
                ESP_OK) {
          registered = true;
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
      }
      if (!registered)
        goto retry_init;
      snprintf(at_cmd, sizeof(at_cmd), "AT+QICSGP=1,1,\"%s\",\"\",\"\",0\r\n",
               CAT1_APN);
      Cat1_Send_AT_Command(at_cmd, 3000, "OK");
      vTaskDelay(pdMS_TO_TICKS(2000));
      if (Cat1_Send_AT_Command("AT+QIACT=1\r\n", 30000, "OK") != ESP_OK)
        goto retry_init;
      vTaskDelay(pdMS_TO_TICKS(5000));
      Cat1_Send_AT_Command("AT+QMTCFG=\"version\",0,4\r\n", 1000, "OK");
      vTaskDelay(pdMS_TO_TICKS(1000));
      if (Cat1_Send_AT_Command("AT+QMTOPEN=0,\"183.230.40.96\",1883\r\n", 15000,
                               "+QMTOPEN: 0,0") != ESP_OK)
        goto retry_init;
      vTaskDelay(pdMS_TO_TICKS(2000));
      MQTT_Init();
      snprintf(at_cmd, sizeof(at_cmd), "AT+QMTCONN=0,\"%s\",\"%s\",\"%s\"\r\n",
               GW_DEVICENAME, GW_PRODUCTID, Mqtt_Password);
      if (Cat1_Send_AT_Command(at_cmd, 10000, "+QMTCONN: 0,0,0") == ESP_OK) {
        SysCB.SysEventFlag |= CONNECT_MQTT;
        WiFi_Cat1_SubOnline(1, 1);
        break;
      }
    retry_init:
      retry_main++;
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
    if (!(SysCB.SysEventFlag & CONNECT_MQTT))
      vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

esp_err_t Cat1_AT_MqttPublish(const char *topic, const char *payload) {
  if (topic == NULL || payload == NULL)
    return ESP_ERR_INVALID_ARG;
  if (SysCB.SysEventFlag & (CONNECT_WIFI | CONNECT_CAT1)) {
    int msg_id = esp_mqtt_publish_msg(topic, payload, strlen(payload), 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
  }
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,0,0,0,\"%s\"\r\n", topic);
  if (Cat1_Send_AT_Command(cmd, 2000, ">") != ESP_OK) {
    SysCB.SysEventFlag &= ~CONNECT_MQTT;
    return ESP_FAIL;
  }
  int total_len = strlen(payload);
  int packet_size = 200;
  int sent_len = 0;
  while (sent_len < total_len) {
    int this_len = (total_len - sent_len > packet_size)
                       ? packet_size
                       : (total_len - sent_len);
    bsp_uart_cat1_send(payload + sent_len, this_len);
    sent_len += this_len;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  char ctrl_z = 0x1A;
  bsp_uart_cat1_send(&ctrl_z, 1);
  return ESP_OK;
}

void start_Cat1Task(void *argument) {
  static uint8_t at_cmd_buf[256];
  uint8_t *data = at_cmd_buf;
  if (g_at_rx_mutex == NULL)
    g_at_rx_mutex = xSemaphoreCreateMutex();
  for (;;) {
    if (SysCB.SysEventFlag & CONNECT_WIFI) {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    int len = uart_read_bytes(UART_NUM_CAT1, data, 255, pdMS_TO_TICKS(100));
    if (len > 0) {
      data[len] = '\0';
      if (g_at_rx_mutex) {
        xSemaphoreTake(g_at_rx_mutex, portMAX_DELAY);
        strncat(g_at_rx_buffer, (char *)data,
                sizeof(g_at_rx_buffer) - strlen(g_at_rx_buffer) - 1);
        g_at_data_ready = true;
        xSemaphoreGive(g_at_rx_mutex);
      }
      if (strstr((char *)data, "RDY")) {
        SysCB.SysEventFlag &= ~CONNECT_MQTT;
      } else if (strstr((char *)data, "+QMTCONN: 0,0,0")) {
        SysCB.SysEventFlag |= CONNECT_MQTT;
      } else if (strstr((char *)data, "+QMTSTAT: 0,")) {
        SysCB.SysEventFlag &= ~CONNECT_MQTT;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  free(data);
  vTaskDelete(NULL);
}


