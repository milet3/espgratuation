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
#include "mw1268_app.h"
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

char TopicBuff[10][128]; // 二维数组，存放需要订阅和发布的主题(Topic)字符串
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
  ESP_LOGD(TAG, "Calculated Password: %s", Mqtt_Password);

  // =======================================================
  // 2. 生成与平台通信所需的 Topic
  // =======================================================
  // 属性设置下发 (必选)
  snprintf(TopicBuff[0], sizeof(TopicBuff[0]), "$sys/%s/%s/thing/property/set",
           GW_PRODUCTID, GW_DEVICENAME);
  // 属性上报回复 (新增：用于排查上报失败原因)
  snprintf(TopicBuff[1], sizeof(TopicBuff[1]),
           "$sys/%s/%s/thing/property/post/reply", GW_PRODUCTID, GW_DEVICENAME);

  // 子设备登录回复 (新增：确保子设备能正常在线)
  snprintf(TopicBuff[2], sizeof(TopicBuff[2]),
           "$sys/%s/%s/thing/sub/login/reply", GW_PRODUCTID, GW_DEVICENAME);

  // 子设备登出回复 (新增)
  snprintf(TopicBuff[3], sizeof(TopicBuff[3]),
           "$sys/%s/%s/thing/sub/logout/reply", GW_PRODUCTID, GW_DEVICENAME);

  // 子设备数据上报回复 (新增)
  snprintf(TopicBuff[4], sizeof(TopicBuff[4]),
           "$sys/%s/%s/thing/sub/property/post/reply", GW_PRODUCTID,
           GW_DEVICENAME);

  // 子设备属性获取回复 (修正：使用下划线 get_reply)
  snprintf(TopicBuff[5], sizeof(TopicBuff[5]),
           "$sys/%s/%s/thing/sub/property/get_reply", GW_PRODUCTID,
           GW_DEVICENAME);

  // 批量上报回复 (新增：用于确认 pack/post 是否成功)
  snprintf(TopicBuff[6], sizeof(TopicBuff[6]),
           "$sys/%s/%s/thing/pack/post/reply", GW_PRODUCTID, GW_DEVICENAME);

  // 子设备属性设置下发 (关键新增：用于接收手机端对 D001 的控制)
  snprintf(TopicBuff[7], sizeof(TopicBuff[7]),
           "$sys/%s/%s/thing/sub/property/set", GW_PRODUCTID, GW_DEVICENAME);

  TopicNum = 8;
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
    ESP_LOGI(TAG, "MQTT 已连接 OneNET");
    SysCB.SysEventFlag |= CONNECT_MQTT;
    disconnect_start_tick = 0; // 连接成功，清空计时器

    // 第一步：订阅所有主题，并使用 QoS 1 确保订阅成功
    for (int i = 0; i < TopicNum; i++) {
      int msg_id = esp_mqtt_client_subscribe(client, TopicBuff[i], 1);
      ESP_LOGD(TAG, "订阅主题 [%s], msg_id=%d", TopicBuff[i], msg_id);
    }

    // [逻辑调整] 仅在 LoRa 通信已确认的情况下才进行上线报备
    if ((SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
        !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
      ESP_LOGI(TAG, "LoRa 已确认，报备子设备上线");
      WiFi_Cat1_SubOnline(1, 1);
      SysCB.SysEventFlag |= SUB_ONLINE_READY;
    } else {
      ESP_LOGI(TAG, "等待 LoRa 确认后再报备子设备上线");
    }

    // [逻辑调整] 仅在 LoRa 通信已确认的情况下才进行上线报备
    if ((SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
        !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
      ESP_LOGI(TAG, "LoRa 已预先确认，立即执行子设备上线报备");
      WiFi_Cat1_SubOnline(1, 1);
      SysCB.SysEventFlag |= SUB_ONLINE_READY;
    } else {
      ESP_LOGW(TAG, "LoRa 尚未确认或已在线，等待 LoRa 任务触发上线报备");
    }
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT 已断开，等待自动重连");
    SysCB.SysEventFlag &= ~CONNECT_MQTT;
    SysCB.SysEventFlag &= ~SUB_ONLINE_READY; // 断连时重置子设备上线标志位

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
    ESP_LOGD(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    // 修正：不再在回调中执行阻塞延迟或报备，仅记录日志
    break;

  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGD(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA: {
    ESP_LOGD(TAG, "收到 MQTT 数据包，长度: %d", event->data_len);

    // 安全处理非空终止的 Topic 字符串
    char topic_tmp[128] = {0};
    int copy_len = event->topic_len < (sizeof(topic_tmp) - 1)
                       ? event->topic_len
                       : (sizeof(topic_tmp) - 1);
    memcpy(topic_tmp, event->topic, copy_len);
    topic_tmp[copy_len] = '\0';

    ESP_LOGD(TAG, "收到主题: %s", topic_tmp);
    ESP_LOGD(TAG, "收到原始数据: %.*s", event->data_len, event->data);

    // --- 业务逻辑处理区 ---
    // 1. 处理子设备上线回复
    if (strstr(topic_tmp, "thing/sub/login/reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root) {
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (code && code->valueint == 200) {
<<<<<<< HEAD
          ESP_LOGI(TAG, "子设备上线成功");
        } else {
          ESP_LOGE(TAG, "子设备上线失败，错误码: %d",
=======
          ESP_LOGW(TAG, ">>> [调试信息] 子设备上线成功！服务器已确认。");
        } else {
          ESP_LOGE(TAG, ">>> [调试信息] 子设备上线失败，错误码: %d",
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
                   code ? code->valueint : -1);
        }
        cJSON_Delete(root);
      }
    }

    // 2. 处理子设备数据上报回复
    if (strstr(topic_tmp, "thing/sub/property/post/reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root) {
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (code && code->valueint == 200) {
<<<<<<< HEAD
          ESP_LOGD(TAG, "子设备数据上报成功");
        } else {
          ESP_LOGE(TAG, "子设备数据上报失败，错误码: %d",
=======
          ESP_LOGW(TAG, ">>> [调试信息] 子设备数据上报成功！");
        } else {
          ESP_LOGE(TAG, ">>> [调试信息] 子设备数据上报失败，错误码: %d",
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
                   code ? code->valueint : -1);
        }
        cJSON_Delete(root);
      }
    }

    // 3. 处理子设备属性获取回复
    if (strstr(topic_tmp, "thing/sub/property/get_reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root) {
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (code && code->valueint == 200) {
          cJSON *data = cJSON_GetObjectItem(root, "data");
          if (data) {
            char *data_str = cJSON_PrintUnformatted(data);
<<<<<<< HEAD
            ESP_LOGD(TAG, "获取子设备最新属性: %s", data_str);
            free(data_str);
          }
        } else {
          ESP_LOGE(TAG, "获取子设备属性失败，错误码: %d",
=======
            ESP_LOGW(TAG, ">>> [调试信息] 成功获取子设备最新属性: %s",
                     data_str);
            free(data_str);
          }
        } else {
          ESP_LOGE(TAG, ">>> [调试信息] 获取子设备属性失败，错误码: %d",
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
                   code ? code->valueint : -1);
        }
        cJSON_Delete(root);
      }
    }

    // 4. 处理批量上报回复 (pack/post)
    if (strstr(topic_tmp, "thing/pack/post/reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root) {
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (code && code->valueint == 200) {
<<<<<<< HEAD
          ESP_LOGD(TAG, "批量数据上报成功");
        } else {
          cJSON *msg = cJSON_GetObjectItem(root, "msg");
          ESP_LOGE(TAG, "批量数据上报失败, code: %d, msg: %s",
=======
          ESP_LOGW(TAG, ">>> [调试信息] 批量数据上报 (Pack/Post) 成功！");
        } else {
          cJSON *msg = cJSON_GetObjectItem(root, "msg");
          ESP_LOGE(TAG, ">>> [调试信息] 批量数据上报失败, code: %d, msg: %s",
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
                   code ? code->valueint : -1,
                   (msg && msg->valuestring) ? msg->valuestring : "unknown");
        }
        cJSON_Delete(root);
      }
    }

    // 5. 处理云端属性设置 (Property Set - 兼容网关和子设备)
    if (strstr(topic_tmp, "thing/property/set") ||
        strstr(topic_tmp, "thing/sub/property/set")) {
<<<<<<< HEAD
      ESP_LOGI(TAG, "收到云端属性设置指令");
=======
      ESP_LOGW(TAG, ">>> 收到云端属性设置指令!");
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root) {
        // 尝试获取消息 ID 用于回复
        char reply_id[32] = "20240513";
        cJSON *msg_id_obj = cJSON_GetObjectItem(root, "id");
        if (msg_id_obj && msg_id_obj->valuestring) {
          strncpy(reply_id, msg_id_obj->valuestring, sizeof(reply_id) - 1);
        }

        cJSON *params = cJSON_GetObjectItem(root, "params");
        if (params) {
          // A. 如果是子设备控制，params 内部可能还有一个 params 嵌套
          cJSON *sub_params = cJSON_GetObjectItem(params, "params");
          cJSON *target_params = sub_params ? sub_params : params;

          // 1. 查找 PestAlarm (ATTRIBUTE1) -> 控制网关 LED
          cJSON *pest_obj = cJSON_GetObjectItem(target_params, ATTRIBUTE1);
          if (pest_obj) {
            int val = pest_obj->valueint;
<<<<<<< HEAD
            ESP_LOGI(TAG, "设置网关 PestAlarm: %d", val);
=======
            ESP_LOGW(TAG, ">>> [控制指令] 设置网关 PestAlarm 为: %d", val);
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
            gpio_set_level(LED_GW001_LED_PIN, val ? 0 : 1);

            // 状态回报
            cJSON *reply_root = cJSON_CreateObject();
            cJSON_AddStringToObject(reply_root, "id", reply_id);
            cJSON_AddStringToObject(reply_root, "version", "1.0");
            cJSON *reply_params = cJSON_AddObjectToObject(reply_root, "params");
            cJSON_AddNumberToObject(reply_params, ATTRIBUTE1, val);

            char *reply_data = cJSON_PrintUnformatted(reply_root);
            if (reply_data) {
              char reply_topic[128];
              snprintf(reply_topic, sizeof(reply_topic),
                       "$sys/%s/%s/thing/property/post", GW_PRODUCTID,
                       GW_DEVICENAME);
              esp_mqtt_publish_msg(reply_topic, reply_data, strlen(reply_data),
                                   1, 0);
              free(reply_data);
            }
            cJSON_Delete(reply_root);
          }

          // 2. 查找 PowerSwitch_2 (ATTRIBUTE2) -> 控制子节点 LED
          cJSON *node_led_obj = cJSON_GetObjectItem(target_params, ATTRIBUTE2);
          if (node_led_obj) {
            int val = node_led_obj->valueint;
<<<<<<< HEAD
            ESP_LOGI(TAG, "设置子节点 LED: %d", val);
=======
            ESP_LOGW(TAG, ">>> [控制指令] 设置子节点 LED 为: %d", val);
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
            LoRa_ControlNodeLED(val);

            // 状态回报 (子设备控制回复同样建议发往网关的主题进行同步)
            cJSON *reply_root = cJSON_CreateObject();
            cJSON_AddStringToObject(reply_root, "id", reply_id);
            cJSON_AddStringToObject(reply_root, "version", "1.0");
            cJSON *reply_params = cJSON_AddObjectToObject(reply_root, "params");
            cJSON_AddNumberToObject(reply_params, ATTRIBUTE2, val);

            char *reply_data = cJSON_PrintUnformatted(reply_root);
            if (reply_data) {
              char reply_topic[128];
              snprintf(reply_topic, sizeof(reply_topic),
                       "$sys/%s/%s/thing/property/post", GW_PRODUCTID,
                       GW_DEVICENAME);
              esp_mqtt_publish_msg(reply_topic, reply_data, strlen(reply_data),
                                   1, 0);
              free(reply_data);
            }
            cJSON_Delete(reply_root);
          }
        }
        cJSON_Delete(root);
      }
    }

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
<<<<<<< HEAD
  if (mqtt_client != NULL) {
    ESP_LOGW(TAG, "MQTT client already initialized, skip duplicate start");
    return ESP_OK;
=======
  // 移除硬编码延时，防止阻塞事件回调导致 WDT 超时
  // ESP_LOGI(TAG, "正在等待网络和 DNS 稳定 (5秒)...");
  // vTaskDelay(pdMS_TO_TICKS(5000));

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
>>>>>>> 3e70c77ee4c2f18d61b4633f3189556fcc6b895d
  }

  // 移除硬编码延时，防止阻塞事件回调导致 WDT 超时
  // ESP_LOGI(TAG, "正在等待网络和 DNS 稳定 (5秒)...");
  // vTaskDelay(pdMS_TO_TICKS(5000));

  // 确保 TopicBuff 已经被初始化
  MQTT_Init();

  // 配置 MQTT 客户端参数
  // 使用 OneNET Studio 标准域名 mqtts.heclouds.com，配合 1883 (TCP) 或 8883
  // (TLS) 【修正】根据日志 "couldn't get hostname for
  // :mqtts.heclouds.com"，必须带上 mqtt:// 协议头
  char target_uri[128];
  snprintf(target_uri, sizeof(target_uri), "mqtt://%s:%d", MQTT_SERVER,
           MQTT_PORT);

  ESP_LOGI(TAG, "正在连接 MQTT: %s", target_uri);

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
      .buffer.out_size = 8192,              // 写缓冲区也增大到 8KB
      .task.stack_size = 8192,              // 增加任务栈大小
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
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    return err;
  }

  // 启动客户端，开始连接服务器
  err = esp_mqtt_client_start(mqtt_client);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "MQTT 客户端已启动");
  } else {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
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
