#include "wifi_manager.h"
#include "bsp_storage.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wifi_cat1.h"
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TAG "WIFI_MANAGER"

/*
 * WiFi 管理模块职责：
 * 1. 启动后优先读取历史 WiFi 配置，存在则直接以 STA 模式连接路由器。
 * 2. 没有历史配置时，启动 AP 配网热点并提供网页配置入口。
 * 3. 配网成功拿到 IP 后，再把 SSID/密码写入项目封装的 EEprom/NVS。
 * 4. 连接成功后关闭 AP、DNS、HTTP 配网服务，回到普通 STA 工作模式。
 */
#define MAX_CONNECT_RETRY 6
#define CAPTIVE_DNS_PORT 53
#define CAPTIVE_DNS_BUF_SIZE 512
#define CAPTIVE_PORTAL_IP "192.168.4.1"
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASSWORD_MAX_LEN 64
#define WIFI_URL_ENCODED_SSID_BUF_SIZE (WIFI_SSID_MAX_LEN * 3 + 1)
#define WIFI_URL_ENCODED_PASSWORD_BUF_SIZE (WIFI_PASSWORD_MAX_LEN * 3 + 1)
#define WIFI_FORM_BUF_SIZE 384
#define WIFI_SCAN_MAX_AP 20
#define WIFI_SCAN_PAGE_BUF_SIZE 8192
static int sta_connect_count = 0; // 连接次数

static p_wifi_state_callback wifi_state_cb = NULL; // WiFi 状态回调函数

static bool is_sta_connected = false; // 是否连接成功

static bool sta_configured = false; // 是否已经设置过 STA 目标路由器参数
static bool is_ap_active = false;   // 当前是否处于 AP 配网状态
static bool pending_credentials_valid = false; // 是否有等待保存的 WiFi 凭据
static wifi_credentials_t pending_credentials = {0};
static TaskHandle_t dns_task_handle = NULL; // DNS 劫持任务句柄
static TaskHandle_t wifi_cleanup_task_handle = NULL; // AP 收尾任务句柄
static volatile bool dns_server_running = false; // 控制 DNS 任务退出
static const char captive_portal_url[] = "http://" CAPTIVE_PORTAL_IP "/";
// AP 配网页展示的是启动配网前缓存的扫描结果，避免手机连接 AP 后再扫描导致断连。
static wifi_ap_record_t scanned_ap_records[WIFI_SCAN_MAX_AP] = {0};
static uint16_t scanned_ap_count = 0;

static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

static void wifi_connected_cleanup_task(void *pvParameters);

/*
 * 保存待确认的 WiFi 凭据。
 * 注意：这里只在 GOT_IP 后调用，表示密码确实能连上路由器。
 * 不在用户提交表单时立即保存，避免错误密码覆盖掉原来的可用配置。
 */
static void save_pending_wifi_credentials(void) {
  if (!pending_credentials_valid) {
    return;
  }

  pending_credentials.magic = WIFI_CREDENTIAL_MAGIC;
  pending_credentials.ssid[sizeof(pending_credentials.ssid) - 1] = '\0';
  pending_credentials.password[sizeof(pending_credentials.password) - 1] = '\0';

  EEprom_WriteData(WIFI_CREDENTIAL_KEY, &pending_credentials,
                   sizeof(pending_credentials));
  ESP_LOGI(TAG, "Saved WiFi credentials to EEprom. SSID:%s",
           pending_credentials.ssid);
  pending_credentials_valid = false;
}

static void cat1_shutdown_task(void *pvParameters) {
  ESP_LOGI(TAG, "切换WiFi模块连接服务器");
  ESP_LOGI(TAG, "将4G Cat1模块关机");
  // 修正：严禁在此处清除 CONNECT_WIFI，否则会导致上报逻辑切换到错误的 4G AT
  // 模式 SysCB.SysEventFlag &= ~CONNECT_WIFI;
  SysCB.SysEventFlag &= ~CONNECT_OTA;
  SysCB.SysEventFlag &= ~CONNECT_CAT1;
  SysCB.SysEventFlag &= ~CONNECT_PING;

  CAT1_POWER(1);                   // 先拉高
  vTaskDelay(pdMS_TO_TICKS(1500)); // 延时
  CAT1_POWER(0);                   // 拉低关机

  vTaskDelete(NULL); // 任务结束删除自己
}

/** WiFi/IP 事件回调函数
 * 这里运行在 ESP-IDF 默认事件循环上下文里，应尽量短小。
 * 耗时动作或可能涉及事件系统的动作，例如 MQTT 启动，要放到外部任务中处理。
 *
 * @param arg   用户传递的参数
 * @param event_base    事件类别
 * @param event_id      事件ID
 * @param event_data    事件携带的数据
 * @return 无
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START: // STA 接口启动完成，开始尝试连接目标路由器
    {
      wifi_mode_t mode;
      esp_wifi_get_mode(&mode);
      if ((mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) && sta_configured)
        esp_wifi_connect(); // 连接 WiFi
      break;
    }
    case WIFI_EVENT_STA_CONNECTED: // 已经连接
    {
      ESP_LOGD(TAG, "物理链路已连接，等待分配IP...");
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: // 断开连接
      is_sta_connected = false;
      SysCB.SysEventFlag &= ~CONNECT_WIFI; // 清除WiFi连接成功事件
      if (wifi_state_cb)
        wifi_state_cb(
            WIFI_STATE_DISCONNECTED); // 调用状态回调函数，通知断开连接

      // 短时间断线时进行有限次数重连，避免一直阻塞在 WiFi 重试中。
      if (sta_connect_count < MAX_CONNECT_RETRY) {
        wifi_mode_t mode;
        esp_wifi_get_mode(&mode);
        if ((mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) && sta_configured)
          esp_wifi_connect(); // 连接 WiFi
        sta_connect_count++;
        ESP_LOGI(TAG, "重连中 (%d/%d)...", sta_connect_count,
                 MAX_CONNECT_RETRY);
      } else {
        ESP_LOGW(TAG, "达到最大重连次数，停止尝试");
        // 这里可以触发切换回 Cat1 的紧急任务
      }
      break;
    case WIFI_EVENT_AP_STACONNECTED: // 有设备连接到AP
    {
      wifi_event_ap_staconnected_t *event =
          (wifi_event_ap_staconnected_t *)event_data;
      ESP_LOGD(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
               event->aid);
      break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: // 有设备断开AP
    {
      wifi_event_ap_stadisconnected_t *event =
          (wifi_event_ap_stadisconnected_t *)event_data;
      ESP_LOGD(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac),
               event->aid);
      break;
    }
    default:
      break;
    }
  } else if (event_base == IP_EVENT) {
    switch (event_id) {
    case IP_EVENT_STA_GOT_IP: // 获取 IP 地址
    {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "已获取IP: " IPSTR, IP2STR(&event->ip_info.ip));
      sta_connect_count = 0;
      is_sta_connected = true;            // 标记为已连接
      SysCB.SysEventFlag |= CONNECT_WIFI; // 置位连接WiFi成功事件
      // 只有拿到 IP 才认为配网真正成功，此时再保存账号密码。
      save_pending_wifi_credentials();
      if (wifi_state_cb)
        wifi_state_cb(WIFI_STATE_CONNECTED); // 调用状态回调函数，通知连接成功

      // 安全地处理硬件操作：不阻塞事件任务
      // 修正：只有当 CAT1 已经上电时才尝试关机，并且增加引脚有效性检查
      if (is_ap_active && wifi_cleanup_task_handle == NULL) {
        xTaskCreate(wifi_connected_cleanup_task, "wifi_cleanup", 3072, NULL, 5,
                    &wifi_cleanup_task_handle);
      }

      if (CAT1_POWER_STATE_PIN >= 0) {
        xTaskCreate(cat1_shutdown_task, "cat1_off", 2048, NULL, 5, NULL);
      }
      break;
    }
    default:
      break;
    }
  }
}

void wifi_set_state_callback(p_wifi_state_callback cb) { wifi_state_cb = cb; }

void wifi_manager_cancel_connect_retry(void) {
  sta_configured = false;
  sta_connect_count = MAX_CONNECT_RETRY;
}

/*
 * 从项目自己的 EEprom/NVS 封装读取历史 WiFi 配置。
 * magic 字段用于判断这块数据是否真的是 WiFi 配置，避免误读未初始化数据。
 */
bool wifi_manager_load_saved_config(wifi_credentials_t *credentials) {
  if (credentials == NULL) {
    return false;
  }

  memset(credentials, 0, sizeof(*credentials));
  EEprom_ReadData(WIFI_CREDENTIAL_KEY, credentials, sizeof(*credentials));

  credentials->ssid[sizeof(credentials->ssid) - 1] = '\0';
  credentials->password[sizeof(credentials->password) - 1] = '\0';

  if (credentials->magic != WIFI_CREDENTIAL_MAGIC ||
      strlen(credentials->ssid) == 0) {
    memset(credentials, 0, sizeof(*credentials));
    return false;
  }

  return true;
}

/** 初始化wifi，默认进入STA模式
 * @param 无
 * @return 无
 */
void wifi_manager_init(void) {
  ESP_ERROR_CHECK(esp_netif_init());                // 初始化网络接口
  ESP_ERROR_CHECK(esp_event_loop_create_default()); // 创建事件循环

  sta_netif = esp_netif_create_default_wifi_sta(); // 创建默认的 WiFi STA 接口
  ap_netif = esp_netif_create_default_wifi_ap(); // 创建默认的 WiFi AP 接口

  wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT(); // 初始化 WiFi 配置
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));                // 初始化 WiFi

  // WiFi 驱动自己的配置只放在 RAM，业务凭据统一由 EEprom_WriteData 管理。
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  // 注册 WiFi 和 IP 事件。连接、断线、获取 IP 都会回到 event_handler。
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             event_handler, NULL));

  ESP_ERROR_CHECK(
      esp_wifi_set_mode(WIFI_MODE_STA)); // 设置 WiFi 模式为 STA 模式
  ESP_ERROR_CHECK(esp_wifi_start());     // 启动 WiFi

  ESP_LOGI(TAG, "WiFi 管理器已初始化"); // 打印初始化信息
}

/** 连接wifi
 * @param ssid
 * @param password
 * @return 成功/失败
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password) {
  if (ssid == NULL || strlen(ssid) == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  if (password == NULL) {
    password = "";
  }

  sta_configured = true;
  // 先把准备连接的账号密码放到 pending_credentials。
  // 是否真正写入 Flash，要等 IP_EVENT_STA_GOT_IP 确认成功。
  memset(&pending_credentials, 0, sizeof(pending_credentials));
  pending_credentials.magic = WIFI_CREDENTIAL_MAGIC;
  snprintf(pending_credentials.ssid, sizeof(pending_credentials.ssid), "%s",
           ssid);
  snprintf(pending_credentials.password,
           sizeof(pending_credentials.password), "%s", password);

  // 如果本次连接参数与历史配置一致，则无需重复写 Flash，减少擦写。
  wifi_credentials_t saved_credentials = {0};
  pending_credentials_valid =
      !wifi_manager_load_saved_config(&saved_credentials) ||
      strcmp(saved_credentials.ssid, pending_credentials.ssid) != 0 ||
      strcmp(saved_credentials.password, pending_credentials.password) != 0;

  sta_connect_count = 0; // 初始化连接次数为0
  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_WPA2_PSK, // 加密方式
          },
  };
  snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s",
           ssid); // 设置 SSID
  snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password),
           "%s", password); // 设置密码

  wifi_mode_t mode;
  esp_wifi_get_mode(&mode);

  // 如果之前不是 STA/APSTA，说明 WiFi 模式不适合直接连接，需要切回 STA。
  if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
    ESP_ERROR_CHECK(esp_wifi_stop()); // 停止 WiFi
    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)); // 设置 WiFi 模式为 STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_config(
        WIFI_IF_STA, &wifi_config));   // 修复了WIFI_IF_STA_STA错误
    ESP_ERROR_CHECK(esp_wifi_start()); // 启动 WiFi
  } else {
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect(); // 连接 WiFi
  }
  return ESP_OK;
}

/** 启动AP热点
 * @param ssid
 * @param password
 * @return 成功/失败
 */
esp_err_t wifi_manager_start_ap(const char *ssid, const char *password) {
  wifi_config_t wifi_config = {
      .ap = {.ssid_len = strlen(ssid),
             .channel = 1,
             .max_connection = 4,
             .authmode = WIFI_AUTH_WPA_WPA2_PSK},
  };

  snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s",
           ssid);

  if (password == NULL || strlen(password) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  } else {
    snprintf((char *)wifi_config.ap.password, sizeof(wifi_config.ap.password),
             "%s", password);
  }

  wifi_mode_t mode;
  esp_wifi_get_mode(&mode);

  if (mode == WIFI_MODE_STA) {
    // 配网阶段使用 APSTA：AP 给手机连，STA 后续连接目标路由器。
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  } else if (mode == WIFI_MODE_NULL) {
    // 如果未配置，直接使用AP模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  }

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  is_ap_active = true;

  if (wifi_state_cb) {
    wifi_state_cb(WIFI_STATE_AP_STARTED);
  }

  ESP_LOGI(TAG, "AP 配网已启动，SSID:%s", ssid);
  return ESP_OK;
}

// ======================= AP 配网部分 =======================
static httpd_handle_t server = NULL;

/*
 * 配置 AP 侧 DHCP 参数。
 * 手机连接 ESP32_Config 后，会从 ESP32 的 DHCP server 获取：
 * - 手机自己的 IP
 * - 网关地址 192.168.4.1
 * - DNS 地址 192.168.4.1
 *
 * 这样手机访问联网检测地址时，会先把 DNS 查询发给 ESP32，
 * ESP32 再把它引导到本机 HTTP 配网页。
 */
static void configure_captive_portal_dhcp(void) {
  if (ap_netif == NULL) {
    return;
  }

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(ap_netif, &ip_info) != ESP_OK) {
    return;
  }

  esp_netif_dhcps_stop(ap_netif);

  // 把 AP 自己的地址作为 DNS server 下发给手机。
  esp_netif_dns_info_t dns_info = {
      .ip =
          {
              .type = ESP_IPADDR_TYPE_V4,
              .u_addr.ip4.addr = ip_info.ip.addr,
          },
  };
  esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);

  // DHCP option 6：告诉客户端 DNS server 是 ESP32。
  uint8_t offer_dns = 1;
  esp_err_t err = esp_netif_dhcps_option(
      ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
      &offer_dns, sizeof(offer_dns));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set AP DHCP DNS option: %s",
             esp_err_to_name(err));
  }

  // DHCP option 114：告诉支持该选项的系统 captive portal 页面地址。
  const char *portal = captive_portal_url;
  err = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_CAPTIVEPORTAL_URI, (void *)portal,
                               strlen(portal));
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to set AP captive portal URI: %s",
             esp_err_to_name(err));
  }

  err = esp_netif_dhcps_start(ap_netif);
  if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
    ESP_LOGW(TAG, "Failed to restart AP DHCP server: %s",
             esp_err_to_name(err));
  }
}

/*
 * 构造一个最小 DNS 响应包。
 * 无论手机查询哪个域名，都回答 192.168.4.1，
 * 从而把浏览器或系统联网检测请求引回 ESP32 自己。
 */
static int dns_build_response(uint8_t *buf, int len) {
  if (len < 12 || (buf[2] & 0x80)) {
    return 0;
  }

  uint16_t flags = ((uint16_t)buf[2] << 8) | buf[3];
  uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
  if (qdcount == 0) {
    return 0;
  }

  int pos = 12;
  while (pos < len && buf[pos] != 0) {
    pos += buf[pos] + 1;
  }
  pos++;

  if (pos + 4 > len || pos + 16 > CAPTIVE_DNS_BUF_SIZE) {
    return 0;
  }

  int query_end = pos + 4;
  buf[2] = 0x80 | (flags & 0x01);
  buf[3] = 0x80;
  buf[6] = 0x00;
  buf[7] = 0x01;
  buf[8] = 0x00;
  buf[9] = 0x00;
  buf[10] = 0x00;
  buf[11] = 0x00;

  uint8_t answer[] = {
      0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
      0x00, 0x1E, 0x00, 0x04, 192,  168,  4,    1,
  };
  memcpy(buf + query_end, answer, sizeof(answer));
  return query_end + sizeof(answer);
}

/*
 * 简易 captive DNS server。
 * 监听 UDP 53 端口，收到 DNS 查询后统一返回 192.168.4.1。
 * 这是手机自动弹出配网页的关键环节之一。
 */
static void captive_dns_task(void *pvParameters) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create DNS socket: errno %d", errno);
    dns_server_running = false;
    dns_task_handle = NULL;
    vTaskDelete(NULL);
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in server_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(CAPTIVE_DNS_PORT),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
    ESP_LOGE(TAG, "Failed to bind DNS socket: errno %d", errno);
    close(sock);
    dns_server_running = false;
    dns_task_handle = NULL;
    vTaskDelete(NULL);
  }

  struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  uint8_t rx_buf[CAPTIVE_DNS_BUF_SIZE];
  while (dns_server_running) {
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(sock, rx_buf, sizeof(rx_buf), 0,
                       (struct sockaddr *)&source_addr, &socklen);
    if (len <= 0) {
      continue;
    }

    int resp_len = dns_build_response(rx_buf, len);
    if (resp_len > 0) {
      sendto(sock, rx_buf, resp_len, 0, (struct sockaddr *)&source_addr,
             sizeof(source_addr));
    }
  }

  close(sock);
  dns_task_handle = NULL;
  vTaskDelete(NULL);
}

static void start_captive_dns_server(void) {
  if (dns_task_handle != NULL) {
    return;
  }

  dns_server_running = true;
  xTaskCreate(captive_dns_task, "captive_dns", 4096, NULL, 5,
              &dns_task_handle);
}

static void stop_captive_dns_server(void) { dns_server_running = false; }

// 把 ESP-IDF 扫描到的认证类型转换成网页上更容易理解的字符串。
static const char *authmode_to_text(wifi_auth_mode_t authmode) {
  switch (authmode) {
  case WIFI_AUTH_OPEN:
    return "OPEN";
  case WIFI_AUTH_WEP:
    return "WEP";
  case WIFI_AUTH_WPA_PSK:
    return "WPA";
  case WIFI_AUTH_WPA2_PSK:
    return "WPA2";
  case WIFI_AUTH_WPA_WPA2_PSK:
    return "WPA/WPA2";
  case WIFI_AUTH_WPA2_ENTERPRISE:
    return "WPA2-ENT";
  case WIFI_AUTH_WPA3_PSK:
    return "WPA3";
  case WIFI_AUTH_WPA2_WPA3_PSK:
    return "WPA2/WPA3";
  default:
    return "SEC";
  }
}

// 输出 SSID 到 HTML 前需要转义，避免 SSID 中的特殊字符破坏页面结构。
static void html_append_escaped(char *dst, size_t dst_size, const char *src) {
  size_t len = strlen(dst);

  while (*src && len + 1 < dst_size) {
    const char *escaped = NULL;
    switch (*src) {
    case '&':
      escaped = "&amp;";
      break;
    case '<':
      escaped = "&lt;";
      break;
    case '>':
      escaped = "&gt;";
      break;
    case '"':
      escaped = "&quot;";
      break;
    case '\'':
      escaped = "&#39;";
      break;
    default:
      dst[len++] = *src++;
      dst[len] = '\0';
      continue;
    }

    size_t escaped_len = strlen(escaped);
    if (len + escaped_len >= dst_size) {
      break;
    }
    memcpy(dst + len, escaped, escaped_len);
    len += escaped_len;
    dst[len] = '\0';
    src++;
  }
}

// 安全地向动态 HTML 缓冲区追加格式化内容。
static size_t html_append(char *dst, size_t dst_size, const char *fmt, ...) {
  size_t len = strlen(dst);
  if (len >= dst_size) {
    return len;
  }

  va_list args;
  va_start(args, fmt);
  vsnprintf(dst + len, dst_size - len, fmt, args);
  va_end(args);
  return strlen(dst);
}

/*
 * 扫描附近 2.4GHz WiFi。
 * ESP32 只能连接 2.4GHz 网络，5GHz-only 路由器不会出现在结果中。
 */
static uint16_t scan_wifi_aps(wifi_ap_record_t *ap_records,
                              uint16_t max_records) {
  if (ap_records == NULL || max_records == 0) {
    return 0;
  }

  wifi_scan_config_t scan_config = {
      .ssid = NULL,
      .bssid = NULL,
      .channel = 0,
      .show_hidden = false,
      .scan_type = WIFI_SCAN_TYPE_ACTIVE,
  };

  esp_err_t err = esp_wifi_scan_start(&scan_config, true);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
    return 0;
  }

  uint16_t ap_count = max_records;
  err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Failed to get WiFi scan records: %s", esp_err_to_name(err));
    return 0;
  }

  ESP_LOGI(TAG, "WiFi scan done, found %u AP(s)", ap_count);
  return ap_count;
}

// 刷新网页要展示的 WiFi 列表缓存。配网 AP 启动前调用最稳定。
static void refresh_wifi_scan_cache(void) {
  memset(scanned_ap_records, 0, sizeof(scanned_ap_records));
  scanned_ap_count = scan_wifi_aps(scanned_ap_records, WIFI_SCAN_MAX_AP);
}

/*
 * 生成配网页 HTML。
 * 页面包含：
 * - ESP32 扫描到的 WiFi 下拉框
 * - 手动 SSID 输入框
 * - WiFi 密码输入框
 */
static esp_err_t __attribute__((unused)) send_wifi_config_page(httpd_req_t *req) {
  char *page = calloc(1, WIFI_SCAN_PAGE_BUF_SIZE);
  if (page == NULL) {
    free(page);
    httpd_resp_send_500(req);
    return ESP_ERR_NO_MEM;
  }

  html_append(page, WIFI_SCAN_PAGE_BUF_SIZE,
              "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>WiFi配网</title>"
              "<style>"
              "body{font-family:Arial,\"Microsoft YaHei\",sans-serif;margin:24px;"
              "background:#f6f7f9;color:#1f2933;}"
              "h2{margin:0 0 18px;font-size:24px;}"
              "label{display:block;margin:14px 0 6px;font-weight:600;}"
              "select,input,button{box-sizing:border-box;width:100%%;font-size:16px;"
              "padding:10px;border:1px solid #ccd3dc;border-radius:6px;}"
              "button{margin-top:18px;background:#1677ff;color:white;border:0;}"
              ".hint{font-size:13px;color:#637083;margin-top:6px;line-height:1.5;}"
              ".wrap{max-width:460px;margin:auto;}"
              "</style></head><body><div class=\"wrap\">"
              "<h2>ESP32 WiFi配置</h2>"
              "<form action=\"/submit\" method=\"post\">"
              "<label for=\"ssid_select\">附近WiFi</label>"
              "<select id=\"ssid_select\" name=\"ssid_select\">");

  if (scanned_ap_count == 0) {
    html_append(page, WIFI_SCAN_PAGE_BUF_SIZE,
                "<option value=\"\">未扫描到WiFi</option>");
  } else {
    for (uint16_t i = 0; i < scanned_ap_count; i++) {
      char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
      snprintf(ssid, sizeof(ssid), "%s", (char *)scanned_ap_records[i].ssid);
      if (strlen(ssid) == 0) {
        continue;
      }

      html_append(page, WIFI_SCAN_PAGE_BUF_SIZE, "<option value=\"");
      html_append_escaped(page, WIFI_SCAN_PAGE_BUF_SIZE, ssid);
      html_append(page, WIFI_SCAN_PAGE_BUF_SIZE, "\">");
      html_append_escaped(page, WIFI_SCAN_PAGE_BUF_SIZE, ssid);
      html_append(page, WIFI_SCAN_PAGE_BUF_SIZE, " (%ddBm %s)</option>",
                  scanned_ap_records[i].rssi,
                  authmode_to_text(scanned_ap_records[i].authmode));
    }
  }

  html_append(page, WIFI_SCAN_PAGE_BUF_SIZE,
              "</select><div class=\"hint\">列表由ESP32启动配网时扫描生成。若目标WiFi未出现，"
              "可在下面手动输入。</div>"
              "<label for=\"ssid\">手动SSID</label>"
              "<input id=\"ssid\" type=\"text\" name=\"ssid\" maxlength=\"32\" "
              "placeholder=\"可选，填写后优先使用\">"
              "<label for=\"password\">WiFi密码</label>"
              "<input id=\"password\" type=\"password\" name=\"password\" "
              "maxlength=\"64\" placeholder=\"开放网络可留空\">"
              "<button type=\"submit\">连接</button>"
              "</form><div class=\"hint\">ESP32 只能连接 2.4GHz WiFi，"
              "如果没有目标网络，请确认路由器开启 2.4GHz 或重启设备重新扫描。</div>"
              "</div></body></html>");

  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
  free(page);
  return ESP_OK;
}

static esp_err_t send_wifi_config_page_modern(httpd_req_t *req) {
  char *page = calloc(1, WIFI_SCAN_PAGE_BUF_SIZE);
  if (page == NULL) {
    httpd_resp_send_500(req);
    return ESP_ERR_NO_MEM;
  }

  html_append(page, WIFI_SCAN_PAGE_BUF_SIZE,
              "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
              "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
              "<title>WiFi配网</title>"
              "<style>"
              ":root{--bg-a:#f4f7ff;--bg-b:#eef4ff;--bg-c:#f8fbff;"
              "--card:#ffffff;--text:#14213d;--muted:#5c6b88;"
              "--line:#d8e1f0;--accent:#2160ff;--accent-strong:#1747c9;}"
              "*{box-sizing:border-box;}"
              "body{margin:0;min-height:100vh;padding:24px 16px;"
              "font-family:Arial,\"Microsoft YaHei\",sans-serif;"
              "background:linear-gradient(180deg,var(--bg-a) 0%%,var(--bg-b) 48%%,var(--bg-c) 100%%);"
              "color:var(--text);}"
              ".wrap{max-width:520px;margin:0 auto;}"
              ".card{background:var(--card);border:1px solid rgba(216,225,240,.9);"
              "border-radius:20px;padding:24px 18px 18px;"
              "box-shadow:0 18px 40px rgba(24,52,99,.10);}"
              ".eyebrow{display:inline-block;padding:6px 10px;border-radius:999px;"
              "background:#e7eeff;color:var(--accent);font-size:12px;font-weight:700;"
              "letter-spacing:.04em;}"
              "h2{margin:14px 0 10px;font-size:28px;line-height:1.2;}"
              ".intro{margin:0 0 18px;font-size:14px;color:var(--muted);line-height:1.6;}"
              ".field{margin-top:14px;}"
              "label{display:block;margin:0 0 8px;font-size:14px;font-weight:700;}"
              "select,input{width:100%%;padding:13px 12px;border:1px solid var(--line);"
              "border-radius:14px;background:#fbfcff;color:var(--text);font-size:16px;outline:none;}"
              "select:focus,input:focus{border-color:var(--accent);"
              "box-shadow:0 0 0 4px rgba(33,96,255,.12);}"
              ".hint{margin:8px 0 0;font-size:12px;color:var(--muted);line-height:1.6;}"
              "button{width:100%%;margin-top:18px;padding:14px 16px;border:0;border-radius:14px;"
              "background:linear-gradient(135deg,var(--accent) 0%%,var(--accent-strong) 100%%);"
              "color:#fff;font-size:16px;font-weight:700;letter-spacing:.02em;}"
              ".footnote{margin:16px 0 0;padding-top:14px;border-top:1px solid #edf1f7;"
              "font-size:12px;color:#7a879d;line-height:1.6;}"
              "</style></head><body><div class=\"wrap\"><div class=\"card\">"
              "<span class=\"eyebrow\">AP 配网</span>"
              "<h2>连接你的 WiFi</h2>"
              "<p class=\"intro\">选择附近的 2.4GHz WiFi，或手动输入网络名称，然后填写密码完成配网。</p>"
              "<form action=\"/submit\" method=\"post\">"
              "<div class=\"field\"><label for=\"ssid_select\">附近 WiFi</label>"
              "<select id=\"ssid_select\" name=\"ssid_select\">");

  if (scanned_ap_count == 0) {
    html_append(page, WIFI_SCAN_PAGE_BUF_SIZE,
                "<option value=\"\">未扫描到 WiFi</option>");
  } else {
    for (uint16_t i = 0; i < scanned_ap_count; i++) {
      char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
      snprintf(ssid, sizeof(ssid), "%s", (char *)scanned_ap_records[i].ssid);
      if (strlen(ssid) == 0) {
        continue;
      }

      html_append(page, WIFI_SCAN_PAGE_BUF_SIZE, "<option value=\"");
      html_append_escaped(page, WIFI_SCAN_PAGE_BUF_SIZE, ssid);
      html_append(page, WIFI_SCAN_PAGE_BUF_SIZE, "\">");
      html_append_escaped(page, WIFI_SCAN_PAGE_BUF_SIZE, ssid);
      html_append(page, WIFI_SCAN_PAGE_BUF_SIZE, " (%ddBm %s)</option>",
                  scanned_ap_records[i].rssi,
                  authmode_to_text(scanned_ap_records[i].authmode));
    }
  }

  html_append(page, WIFI_SCAN_PAGE_BUF_SIZE,
              "</select><p class=\"hint\">这是设备启动配网前缓存的扫描结果。如果没看到目标网络，可以在下面手动输入。</p></div>"
              "<div class=\"field\"><label for=\"ssid\">手动输入 SSID</label>"
              "<input id=\"ssid\" type=\"text\" name=\"ssid\" maxlength=\"32\" "
              "placeholder=\"可选，填写后优先使用这个名称\"></div>"
              "<div class=\"field\"><label for=\"password\">WiFi 密码</label>"
              "<input id=\"password\" type=\"password\" name=\"password\" "
              "maxlength=\"64\" placeholder=\"开放网络可留空\"></div>"
              "<button type=\"submit\">连接并保存</button>"
              "</form><p class=\"footnote\">仅支持 2.4GHz WiFi。如果没有目标网络，请确认路由器已开启 2.4GHz，并让设备重新进入配网模式后再试。</p>"
              "</div></div></body></html>");

  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
  free(page);
  return ESP_OK;
}

/* GET / 响应主页 */
static esp_err_t root_get_handler(httpd_req_t *req) {
  return send_wifi_config_page_modern(req);
}

/* 手机系统会访问这些探测地址；返回配网页可触发 captive portal 自动弹窗。 */
static esp_err_t captive_probe_get_handler(httpd_req_t *req) {
  return root_get_handler(req);
}

/* URL 解码：把表单中的 %XX 和 + 还原成真实 SSID/密码字符。 */
static void url_decode(char *dst, const char *src) {
  char a, b;
  while (*src) {
    if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
        (isxdigit(a) && isxdigit(b))) {
      if (a >= 'a')
        a -= 'a' - 'A';
      if (a >= 'A')
        a -= ('A' - 10);
      else
        a -= '0';
      if (b >= 'a')
        b -= 'a' - 'A';
      if (b >= 'A')
        b -= ('A' - 10);
      else
        b -= '0';
      *dst++ = 16 * a + b;
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst++ = '\0';
}

// 复制表单字段值并保证字符串结尾，避免字段过长导致越界。
static void form_value_copy(char *dst, size_t dst_size, const char *src,
                            size_t src_len) {
  if (dst_size == 0) {
    return;
  }

  size_t copy_len = src_len;
  if (copy_len >= dst_size) {
    copy_len = dst_size - 1;
  }

  memcpy(dst, src, copy_len);
  dst[copy_len] = '\0';
}

// 从 application/x-www-form-urlencoded 表单中取出指定字段。
static void form_get_value(char *dst, size_t dst_size, const char *form,
                           const char *name) {
  char pattern[32] = {0};
  snprintf(pattern, sizeof(pattern), "%s=", name);

  char *value_start = strstr(form, pattern);
  if (value_start == NULL) {
    return;
  }

  value_start += strlen(pattern);
  char *value_end = strchr(value_start, '&');
  if (value_end != NULL) {
    form_value_copy(dst, dst_size, value_start, value_end - value_start);
  } else {
    form_value_copy(dst, dst_size, value_start, strlen(value_start));
  }
}

/*
 * POST /submit 处理手机提交的 WiFi 信息。
 * 手动 SSID 优先；如果手动 SSID 为空，则使用下拉框选择的 ssid_select。
 */
static esp_err_t submit_post_handler(httpd_req_t *req) {
  char buf[WIFI_FORM_BUF_SIZE];
  int received = 0;
  int remaining = req->content_len;

  if (remaining >= sizeof(buf)) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form data too large");
    return ESP_FAIL;
  }
  if (remaining <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty request");
    return ESP_FAIL;
  }

  // HTTP body 可能分多次到达，因此循环读取直到完整接收。
  while (remaining > 0) {
    int ret = httpd_req_recv(req, buf + received, remaining);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      return ESP_FAIL;
    }
    received += ret;
    remaining -= ret;
  }
  buf[received] = '\0';

  ESP_LOGD(TAG, "Received form data: %s", buf);

  char ssid_select_raw[WIFI_URL_ENCODED_SSID_BUF_SIZE] = {0};
  char ssid_raw[WIFI_URL_ENCODED_SSID_BUF_SIZE] = {0};
  char password_raw[WIFI_URL_ENCODED_PASSWORD_BUF_SIZE] = {0};
  char ssid_select[WIFI_SSID_MAX_LEN + 1] = {0};
  char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
  char password[WIFI_PASSWORD_MAX_LEN + 1] = {0};

  form_get_value(ssid_select_raw, sizeof(ssid_select_raw), buf, "ssid_select");
  form_get_value(ssid_raw, sizeof(ssid_raw), buf, "ssid");
  form_get_value(password_raw, sizeof(password_raw), buf, "password");

  url_decode(ssid_select, ssid_select_raw);
  url_decode(ssid, ssid_raw);
  url_decode(password, password_raw);

  // 用户没有手动输入时，使用扫描列表里选中的 SSID。
  if (strlen(ssid) == 0 && strlen(ssid_select) > 0) {
    snprintf(ssid, sizeof(ssid), "%s", ssid_select);
  }

  if (strlen(ssid) == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid is empty");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "收到配网信息，目标 SSID:%s", ssid);

  const char *resp_str =
      "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
      "<title>正在连接WiFi</title>"
      "<style>"
      "body{margin:0;min-height:100vh;display:flex;align-items:flex-start;"
      "justify-content:center;background:#f6f7f9;color:#1f2933;"
      "font-family:Arial,\"Microsoft YaHei\",sans-serif;}"
      ".panel{width:calc(100% - 40px);max-width:480px;margin-top:72px;"
      "background:#fff;border-radius:8px;padding:28px 22px;"
      "box-shadow:0 8px 24px rgba(15,23,42,.12);text-align:center;}"
      ".mark{width:56px;height:56px;border-radius:50%;margin:0 auto 18px;"
      "background:#1677ff;color:#fff;display:flex;align-items:center;"
      "justify-content:center;font-size:34px;font-weight:700;}"
      "h2{margin:0 0 12px;font-size:26px;font-weight:700;}"
      "p{margin:0;color:#52606d;font-size:18px;line-height:1.7;}"
      "</style></head><body><div class=\"panel\">"
      "<div class=\"mark\">✓</div>"
      "<h2>WiFi信息已收到</h2>"
      "<p>设备正在连接路由器，请留意设备指示灯。连接成功后，ESP32会自动关闭配网热点。</p>"
      "</div></body></html>";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);

  // 延迟一小段时间后尝试连接（让HTTP响应先发送出去）
  vTaskDelay(pdMS_TO_TICKS(1000));

  // 连接到新的WiFi
  wifi_manager_connect(ssid, password);

  return ESP_OK;
}

static const httpd_uri_t root_uri = {.uri = "/",
                                     .method = HTTP_GET,
                                     .handler = root_get_handler,
                                     .user_ctx = NULL};

static const httpd_uri_t submit_uri = {.uri = "/submit",
                                       .method = HTTP_POST,
                                       .handler = submit_post_handler,
                                       .user_ctx = NULL};

static const httpd_uri_t android_probe_uri = {
    .uri = "/generate_204",
    .method = HTTP_GET,
    .handler = captive_probe_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t android_probe_uri_2 = {
    .uri = "/gen_204",
    .method = HTTP_GET,
    .handler = captive_probe_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t ios_probe_uri = {.uri = "/hotspot-detect.html",
                                          .method = HTTP_GET,
                                          .handler = captive_probe_get_handler,
                                          .user_ctx = NULL};

static const httpd_uri_t apple_probe_uri = {.uri = "/library/test/success.html",
                                            .method = HTTP_GET,
                                            .handler = captive_probe_get_handler,
                                            .user_ctx = NULL};

static const httpd_uri_t windows_probe_uri = {
    .uri = "/connecttest.txt",
    .method = HTTP_GET,
    .handler = captive_probe_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t windows_probe_uri_2 = {
    .uri = "/ncsi.txt",
    .method = HTTP_GET,
    .handler = captive_probe_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t portal_uri = {.uri = "/*",
                                       .method = HTTP_GET,
                                       .handler = root_get_handler,
                                       .user_ctx = NULL};

/*
 * 启动配网 Web 服务器。
 * 这里注册了不同手机系统常用的联网检测 URL：
 * Android: /generate_204, /gen_204
 * iOS: /hotspot-detect.html, /library/test/success.html
 * Windows: /connecttest.txt, /ncsi.txt
 * 返回内容不是系统预期值时，手机通常会自动弹出登录/配网页。
 */
static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.max_uri_handlers = 12;
  httpd_handle_t server = NULL;

  ESP_LOGD(TAG, "Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    ESP_LOGD(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &submit_uri);
    httpd_register_uri_handler(server, &android_probe_uri);
    httpd_register_uri_handler(server, &android_probe_uri_2);
    httpd_register_uri_handler(server, &ios_probe_uri);
    httpd_register_uri_handler(server, &apple_probe_uri);
    httpd_register_uri_handler(server, &windows_probe_uri);
    httpd_register_uri_handler(server, &windows_probe_uri_2);
    httpd_register_uri_handler(server, &portal_uri);
    return server;
  }

  ESP_LOGE(TAG, "Error starting server!");
  return NULL;
}

// 停止 HTTP server。配网成功后必须关闭，避免 AP 已关但 server 句柄残留。
static void stop_webserver(void) {
  if (server != NULL) {
    httpd_stop(server);
    server = NULL;
  }
}

/*
 * WiFi 连接成功后的 AP 配网收尾任务。
 * 不直接在事件回调里 stop server / 切模式，是为了避免阻塞默认事件循环。
 */
static void wifi_connected_cleanup_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(1500));

  stop_captive_dns_server();
  stop_webserver();

  wifi_mode_t mode;
  if (esp_wifi_get_mode(&mode) == ESP_OK && mode == WIFI_MODE_APSTA) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  } else if (mode == WIFI_MODE_AP) {
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
  }

  is_ap_active = false;
  wifi_cleanup_task_handle = NULL;
  ESP_LOGI(TAG, "WiFi 已连接，AP 配网已关闭");
  vTaskDelete(NULL);
}

/** 开启 AP 配网服务
 *  开启热点，并启动 DNS + HTTP 配网服务供手机填写 WiFi 密码
 */
esp_err_t wifi_manager_start_ap_provisioning(const char *ap_ssid,
                                             const char *ap_password) {
  // 先在 STA 模式下扫描一次附近 2.4GHz WiFi，再开启 AP。
  // 手机连上 ESP32 AP 后再扫描会切换信道，容易导致网页超时或扫描结果为空。
  refresh_wifi_scan_cache();

  // 1. 先开启AP热点
  esp_err_t err = wifi_manager_start_ap(ap_ssid, ap_password);
  if (err != ESP_OK) {
    return err;
  }
  configure_captive_portal_dhcp();

  // 2. 启动HTTP服务器
  if (server == NULL) {
    server = start_webserver();
  }
  start_captive_dns_server();

  ESP_LOGI(TAG,
           "AP Provisioning Started. Connect to AP '%s' and visit "
           "http://192.168.4.1",
           ap_ssid);
  return ESP_OK;
}
