/*-------------------------------------------------*/
/*                                                 */
/*       实现 ESP-MQTT 客户端功能的源文件            */
/*   将原 STM32 的手动 MQTT 组包重构为 ESP-IDF API   */
/*                                                 */
/*-------------------------------------------------*/

#include "esp_mqtt.h"
#include "app_config.h" // 引入全局宏，如 GW_DEVICENAME, GW_PRODUCTID 等
#include "cJSON.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "wifi_cat1.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// 如果你需要使用鉴权算法，需要包含你原有的 utils_hmac 相关的头文件
// 假设你已经将 utils_hmac.c 和 utils_hmac.h 添加到了该组件中
#include "utils_hmac.h"

// 如果代码依赖以下外部定义，请确保在 app_config.h 中有对应定义
#ifndef GW_DEVICENAME
#define GW_DEVICENAME "your_device_name"
#endif
#ifndef GW_PRODUCTID
#define GW_PRODUCTID "your_product_id"
#endif

static const char *TAG = "ESP_MQTT";
static esp_mqtt_client_handle_t mqtt_client = NULL; // MQTT 客户端句柄
extern Sys_CB SysCB;

// 新增：断连自动复位计数
static uint32_t disconnect_start_tick = 0;
#define MQTT_DISCONNECT_RESET_TIMEOUT_MS (180000) // 3分钟无法连接则重启网络

char TopicBuff[7][128]; // 二维数组，存放需要订阅和发布的主题(Topic)字符串
char TopicNum; // 记录实际使用的主题数量
char Mqtt_Password[512]; // 存放计算出来的鉴权密码 (扩大到 512 字节防截断)

// 移植原有的 URL_encode 数组和函数
static char tempdata[8] = {'+', ' ', '/', '?', '%', '#', '&', '='};
static char URLdata[8][4] = {"%2B", "%20", "%2F", "%3F",
                             "%25", "%23", "%26", "%3D"};
void URL_encode(char *data, int data_len, char *outdata) {
  int i, j, k = 0;
  for (i = 0; i < data_len; i++) {
    for (j = 0; j < 8; j++) {
      if (data[i] == tempdata[j]) {
        break;
      }
    }
    if (j < 8) {
      memcpy(&outdata[k], URLdata[j], 3);
      k += 3;
    } else {
      outdata[k] = data[i];
      k++;
    }
  }
  outdata[k] = '\0'; // 确保字符串结束
}

/**
 * @brief 初始化 MQTT 参数
 *        主要用于提前拼接和准备需要订阅/发布的主题字符串
 */
void MQTT_Init(void) {
  Token_CB token;
  memset(&token, 0, sizeof(token));

  // =======================================================
  // 1. 恢复原有的 HMAC-SHA1 鉴权密码计算逻辑
  // =======================================================
  int key_len =
      base64_decode(GW_DEVICESECRET, (unsigned char *)token.decodekey);
  sprintf(token.StringForSignature,
          "%s\nsha1\nproducts/%s/devices/%s\n2018-10-31", UNIX, GW_PRODUCTID,
          GW_DEVICENAME);

  ESP_LOGD(TAG, "StringForSignature: %s", token.StringForSignature);

  // OneNet 要求对二进制摘要进行 Base64 编码
  // 注意：在本项目的 utils_hmac.c 中，utils_hmac_sha1_hex 实际上输出的是 20
  // 字节二进制
  utils_hmac_sha1_hex(token.StringForSignature,
                      strlen(token.StringForSignature), token.signtemp,
                      token.decodekey, key_len); // 使用解码后的实际长度 key_len

  // 对 20 字节二进制摘要进行 Base64 编码
  base64_encode((unsigned char *)token.signtemp, token.sign, 20);

  sprintf(token.res, "products/%s/devices/%s", GW_PRODUCTID, GW_DEVICENAME);
  URL_encode(token.sign, strlen(token.sign), token.signURL);
  URL_encode(token.res, strlen(token.res), token.resURL);

  // 将计算结果保存到全局变量，供 esp_mqtt_app_start 使用
  // 使用 snprintf 替代 sprintf 以防止编译器报 format-overflow 警告
  snprintf(Mqtt_Password, sizeof(Mqtt_Password),
           "version=2018-10-31&res=%s&et=%s&method=sha1&sign=%s", token.resURL,
           UNIX, token.signURL);
  ESP_LOGI(TAG, "Calculated Password: %s", Mqtt_Password);

  // =======================================================
  // 2. 生成与平台通信所需的 Topic
  // =======================================================
  // 属性设置下发 (必选)
  snprintf(TopicBuff[0], sizeof(TopicBuff[0]), "$sys/%s/%s/thing/property/set",
           GW_PRODUCTID, GW_DEVICENAME);

  TopicNum = 1;
}

/**
 * @brief MQTT 事件回调处理函数
 *        ESP-MQTT 库在底层发生状态改变或收到数据时，会自动调用此函数
 *
 * @param handler_args 用户传递的参数
 * @param base         事件基(Event Base)
 * @param event_id     事件ID，如连接成功、断开、收到数据等
 * @param event_data   事件数据，包含客户端句柄、收到数据的长度和内容等
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    SysCB.SysEventFlag |= CONNECT_MQTT;
    disconnect_start_tick = 0; // 连接成功，清空计时器

    // 第一步：订阅所有主题，并使用 QoS 1 确保订阅成功
    for (int i = 0; i < TopicNum; i++) {
      int msg_id = esp_mqtt_client_subscribe(client, TopicBuff[i], 1);
      ESP_LOGI(TAG, "正在订阅主题 [%s], msg_id=%d", TopicBuff[i], msg_id);
    }
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    SysCB.SysEventFlag &= ~CONNECT_MQTT;

    // 如果不在 OTA 升级中，则进行重连超时判断
    if (!(SysCB.SysEventFlag & OTA_RUNNING)) {
      if (disconnect_start_tick == 0) {
        disconnect_start_tick = xTaskGetTickCount();
      } else {
        uint32_t diff =
            (xTaskGetTickCount() - disconnect_start_tick) * portTICK_PERIOD_MS;
        if (diff > MQTT_DISCONNECT_RESET_TIMEOUT_MS) {
          ESP_LOGE(TAG, "MQTT 连续断连超过 %d 秒，强制重启网络模块!",
                   MQTT_DISCONNECT_RESET_TIMEOUT_MS / 1000);
          disconnect_start_tick = 0;
          // 注意：此处需要包含 wifi_cat1.h 或声明外部复位函数
          extern void Cat1_Reset(void);
          Cat1_Reset();
        }
      }
    }
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    // 修正：不再在回调中执行阻塞延迟或报备，仅记录日志
    break;

  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA: {
    ESP_LOGW(TAG, "!!! 收到 MQTT 数据包 !!!");
    ESP_LOGI(TAG, "数据长度: %d", event->data_len);

    // 安全处理非空终止的 Topic 字符串
    char topic_tmp[128] = {0};
    int copy_len = event->topic_len < (sizeof(topic_tmp) - 1)
                       ? event->topic_len
                       : (sizeof(topic_tmp) - 1);
    memcpy(topic_tmp, event->topic, copy_len);
    topic_tmp[copy_len] = '\0';

    ESP_LOGW(TAG, "收到主题: %s", topic_tmp);
    ESP_LOGI(TAG, "收到原始数据: %.*s", event->data_len, event->data);

    // --- 业务逻辑处理区 ---
    // 目前仅处理属性设置等基础逻辑，OTA 已改为开机主动查询架构
    break;
  }

  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT_EVENT_ERROR - 详细错误信息:");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      ESP_LOGE(TAG, "  错误类型: TCP 传输错误");
      ESP_LOGE(TAG, "  esp-tls 错误码: 0x%x",
               event->error_handle->esp_tls_last_esp_err);
      ESP_LOGE(TAG, "  TLS 栈错误码: 0x%x",
               event->error_handle->esp_tls_stack_err);
      ESP_LOGE(TAG, "  Socket errno: %d (%s)",
               event->error_handle->esp_transport_sock_errno,
               strerror(event->error_handle->esp_transport_sock_errno));
    } else if (event->error_handle->error_type ==
               MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
      ESP_LOGE(TAG, "  错误类型: 服务器拒绝连接");
      ESP_LOGE(TAG, "  拒绝原因码: 0x%x",
               event->error_handle->connect_return_code);
    } else {
      ESP_LOGE(TAG, "  错误类型: 其他错误 (Type: %d)",
               event->error_handle->error_type);
    }
    break;

  default:
    ESP_LOGI(TAG, "Other event id:%d", event->event_id);
    break;
  }
}

/**
 * @brief 启动 MQTT 客户端连接
 *
 * @param broker_uri MQTT 服务器的 URI 地址 (例如 "mqtt://192.168.1.100:1883")
 * @return esp_err_t 返回 ESP_OK 表示启动成功
 */
esp_err_t esp_mqtt_app_start(const char *broker_uri) {
  // 增加延时，确保 PPP 链路完全稳定后再启动 MQTT
  // 4G 拨号后，网络栈和 DNS 可能需要几秒钟才能完全可用
  ESP_LOGI(TAG, "正在等待网络和 DNS 稳定 (5秒)...");
  vTaskDelay(pdMS_TO_TICKS(5000));

  // 检查网络状态 (调试用)
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("PPP_DEF");
  if (netif) {
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
      ESP_LOGI(TAG, "当前 PPP 接口 IP: " IPSTR, IP2STR(&ip_info.ip));
    } else {
      ESP_LOGW(TAG, "无法获取 PPP 接口 IP 信息");
    }
  } else {
    ESP_LOGW(TAG, "未找到 PPP 网络接口句柄");
  }

  // 确保 TopicBuff 已经被初始化
  MQTT_Init();

  // 配置 MQTT 客户端参数
  // 使用 OneNET Studio 标准域名 mqtts.heclouds.com，配合 1883 (TCP) 或 8883
  // (TLS) 【修正】根据日志 "couldn't get hostname for
  // :mqtts.heclouds.com"，必须带上 mqtt:// 协议头
  char target_uri[128];
  snprintf(target_uri, sizeof(target_uri), "mqtt://%s:%d", MQTT_SERVER,
           MQTT_PORT);

  ESP_LOGI(TAG, "正在初始化 MQTT 客户端, URI: %s", target_uri);

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = target_uri,       // 使用完整的 URI 格式
      .credentials.client_id = GW_DEVICENAME, // 客户端ID (DeviceName)
      .credentials.username = GW_PRODUCTID,   // 用户名 (ProductID)
      .credentials.authentication.password = Mqtt_Password,
      .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1, // 明确使用 MQTT 3.1.1
      .session.keepalive = 120,    // 设置 KeepAlive 为 120 秒
      .network.timeout_ms = 20000, // 增加网络超时时间到 20 秒
      .network.reconnect_timeout_ms = 5000, // 重连间隔
      .buffer.size = 8192,                  // 极限增大读缓冲区 (8KB)
      .buffer.out_size = 4096,              // 写缓冲区
      .task.stack_size =
          8192, // 修正：在 ESP-IDF v5.x 中，栈大小字段为 .task.stack_size
  };

  // 初始化客户端
  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize MQTT client");
    return ESP_FAIL;
  }

  // 注册 MQTT 事件回调函数
  esp_err_t err = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register MQTT event: %s", esp_err_to_name(err));
    return err;
  }

  // 为排查连接问题，开启全方位底层驱动调试日志
  esp_log_level_set("mqtt_client", ESP_LOG_DEBUG);
  esp_log_level_set("transport_tcp", ESP_LOG_DEBUG);
  esp_log_level_set("transport", ESP_LOG_DEBUG);
  esp_log_level_set("outbox", ESP_LOG_DEBUG);
  esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
  esp_log_level_set("mqtt_common", ESP_LOG_DEBUG);

  // 启动客户端，开始连接服务器
  err = esp_mqtt_client_start(mqtt_client);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "MQTT client started");
  } else {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
  }

  return err;
}

/**
 * @brief 停止并销毁 MQTT 客户端
 *        用于设备断网或休眠前释放资源
 */
void esp_mqtt_app_stop(void) {
  if (mqtt_client != NULL) {
    esp_mqtt_client_stop(mqtt_client);    // 停止连接
    esp_mqtt_client_destroy(mqtt_client); // 销毁句柄释放内存
    mqtt_client = NULL;
    ESP_LOGI(TAG, "MQTT client stopped and destroyed");
  }
}

/**
 * @brief 发布 MQTT 消息到指定主题
 *
 * @param topic  目标主题
 * @param data   要发送的数据包指针
 * @param len    数据包长度
 * @param qos    服务质量等级 (0: 最多一次, 1: 至少一次, 2: 只有一次)
 * @param retain 是否作为保留消息 (通常填 0)
 * @return int   成功返回报文的 message_id，失败返回 -1
 */
int esp_mqtt_publish_msg(const char *topic, const char *data, int len, int qos,
                         int retain) {
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "MQTT 客户端未初始化");
    return -1;
  }

  // 检查连接状态
  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGW(TAG, "MQTT 未连接，丢弃主题 [%s] 的消息", topic);
    return -3;
  }

  // 安全检查：如果正在进行 OTA 升级，拦截所有发布请求
  // 这可以防止在上报过程中因 4G 网络拥塞导致的 transport 错误
  if (SysCB.SysEventFlag & OTA_RUNNING) {
    ESP_LOGW(TAG, "OTA 正在运行，拦截主题 [%s] 的发布请求", topic);
    return -2;
  }

  // 调用底层 API 发布消息
  int msg_id =
      esp_mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "MQTT 发布失败, topic=%s, err=%d", topic, msg_id);
  }
  return msg_id;
}
