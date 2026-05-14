#include "wifi_manager.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_cat1.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define TAG "WIFI_MANAGER"

#define MAX_CONNECT_RETRY 6
static int sta_connect_count = 0; // 连接次数

static p_wifi_state_callback wifi_state_cb = NULL; // WiFi 状态回调函数

static bool is_sta_connected = false; // 是否连接成功

static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

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

/** 事件回调函数
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
    case WIFI_EVENT_STA_START: // 连接成功
    {
      wifi_mode_t mode;
      esp_wifi_get_mode(&mode);
      if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
        esp_wifi_connect(); // 连接 WiFi
      break;
    }
    case WIFI_EVENT_STA_CONNECTED: // 已经连接
    {
      ESP_LOGI(TAG, "物理链路已连接，等待分配IP...");
      break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: // 断开连接
      is_sta_connected = false;
      SysCB.SysEventFlag &= ~CONNECT_WIFI; // 清除WiFi连接成功事件
      if (wifi_state_cb)
        wifi_state_cb(
            WIFI_STATE_DISCONNECTED); // 调用状态回调函数，通知断开连接

      if (sta_connect_count < MAX_CONNECT_RETRY) {
        wifi_mode_t mode;
        esp_wifi_get_mode(&mode);
        if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
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
      ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac),
               event->aid);
      break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: // 有设备断开AP
    {
      wifi_event_ap_stadisconnected_t *event =
          (wifi_event_ap_stadisconnected_t *)event_data;
      ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d", MAC2STR(event->mac),
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
      if (wifi_state_cb)
        wifi_state_cb(WIFI_STATE_CONNECTED); // 调用状态回调函数，通知连接成功

      // 安全地处理硬件操作：不阻塞事件任务
      // 修正：只有当 CAT1 已经上电时才尝试关机，并且增加引脚有效性检查
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

  // 注册统一的事件处理函数
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             event_handler, NULL));

  ESP_ERROR_CHECK(
      esp_wifi_set_mode(WIFI_MODE_STA)); // 设置 WiFi 模式为 STA 模式
  ESP_ERROR_CHECK(esp_wifi_start());     // 启动 WiFi

  ESP_LOGI(TAG, "WiFi manager initialized"); // 打印初始化信息
}

/** 连接wifi
 * @param ssid
 * @param password
 * @return 成功/失败
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password) {
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

  // 如果之前不是STA模式或者APSTA模式，停止后重启
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
    // 如果当前是STA模式，则切换到AP+STA模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  } else if (mode == WIFI_MODE_NULL) {
    // 如果未配置，直接使用AP模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  }

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

  if (wifi_state_cb) {
    wifi_state_cb(WIFI_STATE_AP_STARTED);
  }

  ESP_LOGI(TAG, "AP Started. SSID:%s password:%s", ssid, password);
  return ESP_OK;
}

// ======================= AP 配网部分 =======================
static httpd_handle_t server = NULL;

/* 网页 HTML 内容 */
static const char *html_page =
    "<!DOCTYPE html>"
    "<html><head><meta charset=\"UTF-8\"><title>WiFi配网</title></head>"
    "<body><h2>ESP32 WiFi配置</h2>"
    "<form action=\"/submit\" method=\"post\">"
    "WiFi名称(SSID):<br><input type=\"text\" name=\"ssid\" required><br><br>"
    "WiFi密码(Password):<br><input type=\"password\" name=\"password\"><br><br>"
    "<input type=\"submit\" value=\"连接\">"
    "</form></body></html>";

/* GET / 响应主页 */
static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

/* 提取表单数据的辅助函数 */
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

/* POST /submit 处理提交的WiFi信息 */
static esp_err_t submit_post_handler(httpd_req_t *req) {
  char buf[100];
  int ret, remaining = req->content_len;

  if (remaining >= sizeof(buf)) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_408(req);
    }
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  ESP_LOGI(TAG, "Received form data: %s", buf);

  char ssid_raw[32] = {0};
  char password_raw[64] = {0};
  char ssid[32] = {0};
  char password[64] = {0};

  // 简单解析 ssid=...&password=...
  char *ssid_start = strstr(buf, "ssid=");
  if (ssid_start) {
    ssid_start += 5;
    char *ssid_end = strchr(ssid_start, '&');
    if (ssid_end) {
      strncpy(ssid_raw, ssid_start, ssid_end - ssid_start);
    } else {
      strcpy(ssid_raw, ssid_start);
    }
  }

  char *pwd_start = strstr(buf, "password=");
  if (pwd_start) {
    pwd_start += 9;
    char *pwd_end = strchr(pwd_start, '&');
    if (pwd_end) {
      strncpy(password_raw, pwd_start, pwd_end - pwd_start);
    } else {
      strcpy(password_raw, pwd_start);
    }
  }

  url_decode(ssid, ssid_raw);
  url_decode(password, password_raw);

  ESP_LOGI(TAG, "Parsed SSID: %s, Password: %s", ssid, password);

  const char *resp_str =
      "WiFi信息已收到，设备正在连接...请留意设备指示灯或重启设备。";
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

/* 启动配网 Web 服务器 */
static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&server, &config) == ESP_OK) {
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &submit_uri);
    return server;
  }

  ESP_LOGI(TAG, "Error starting server!");
  return NULL;
}

/** 开启 AP 配网服务
 *  开启热点，并启动一个网页服务器供手机填写 WiFi 密码
 */
esp_err_t wifi_manager_start_ap_provisioning(const char *ap_ssid,
                                             const char *ap_password) {
  // 1. 先开启AP热点
  esp_err_t err = wifi_manager_start_ap(ap_ssid, ap_password);
  if (err != ESP_OK) {
    return err;
  }

  // 2. 启动HTTP服务器
  if (server == NULL) {
    server = start_webserver();
  }

  ESP_LOGI(TAG,
           "AP Provisioning Started. Connect to AP '%s' and visit "
           "http://192.168.4.1",
           ap_ssid);
  return ESP_OK;
}