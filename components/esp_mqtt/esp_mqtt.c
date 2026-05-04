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

/**
 * @brief 处理 OTA 升级通知
 * @param data JSON 格式的通知数据
 * @param len 数据长度
 */
static void Process_OTA_Inform(const char *data, int len) {
  cJSON *root = cJSON_ParseWithLength(data, len);
  if (root == NULL) {
    ESP_LOGE(TAG, "解析 OTA JSON 失败");
    return;
  }

  cJSON *params = cJSON_GetObjectItem(root, "params");
  if (params) {
    cJSON *url = cJSON_GetObjectItem(params, "url");
    if (url && cJSON_IsString(url)) {
      ESP_LOGI(TAG, "收到 OTA 升级通知, URL: %s", url->valuestring);
      // 默认 ota_staflag 为 0 (网关自己升级)
      uint8_t ota_staflag = 0;
      cJSON *type = cJSON_GetObjectItem(params, "type");
      if (type && cJSON_IsNumber(type)) {
        ota_staflag = (uint8_t)type->valueint;
      }
      // 触发下载任务
      WiFi_Cat1_StartOTA(url->valuestring, ota_staflag);
    }
  }
  cJSON_Delete(root);
}

char TopicBuff[6][128]; // 二维数组，存放需要订阅和发布的主题(Topic)字符串
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
  base64_decode(GW_DEVICESECRET, (unsigned char *)token.decodekey);
  sprintf(token.StringForSignature,
          "%s\nsha1\nproducts/%s/devices/%s\n2018-10-31", UNIX, GW_PRODUCTID,
          GW_DEVICENAME);
  utils_hmac_sha1_hex(token.StringForSignature,
                      strlen(token.StringForSignature), token.signtemp,
                      token.decodekey, strlen(token.decodekey));
  base64_encode((unsigned char *)token.signtemp, token.sign,
                strlen(token.signtemp));
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
  // 使用 snprintf 替代 sprintf 以防止缓冲区溢出
  snprintf(TopicBuff[0], sizeof(TopicBuff[0]), "$sys/%s/%s/thing/property/set",
           GW_PRODUCTID, GW_DEVICENAME); // 属性设置下发
  snprintf(TopicBuff[1], sizeof(TopicBuff[1]),
           "$sys/%s/%s/thing/sub/login/reply", GW_PRODUCTID,
           GW_DEVICENAME); // 子设备登录应答
  snprintf(TopicBuff[2], sizeof(TopicBuff[2]),
           "$sys/%s/%s/thing/sub/logout/reply", GW_PRODUCTID,
           GW_DEVICENAME); // 子设备登出应答
  snprintf(TopicBuff[3], sizeof(TopicBuff[3]),
           "$sys/%s/%s/thing/pack/post/reply", GW_PRODUCTID,
           GW_DEVICENAME); // 数据打包上报应答
  snprintf(TopicBuff[4], sizeof(TopicBuff[4]),
           "$sys/%s/%s/thing/sub/property/set", GW_PRODUCTID,
           GW_DEVICENAME); // 子设备属性设置下发
  snprintf(TopicBuff[5], sizeof(TopicBuff[5]), "$sys/%s/%s/ota/inform",
           GW_PRODUCTID, GW_DEVICENAME); // OTA 升级通知
  TopicNum = 6;

  // 注意：在 ESP-IDF 的 MQTT 库中，不需要再手动拼接 0x10, 0x82 等底层的 MQTT
  // 16进制报文。 那些基于底层数组的拼接函数全部被 ESP-IDF 内部的 mqtt_client
  // API 取代。
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
    // 连接成功后，遍历订阅 TopicBuff 中的所有主题
    for (int i = 0; i < TopicNum; i++) {
      // 参数说明：(客户端句柄, 主题字符串, QoS等级)
      esp_mqtt_client_subscribe(client, TopicBuff[i], 0);
      ESP_LOGI(TAG, "Subscribed to %s", TopicBuff[i]);
    }
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    SysCB.SysEventFlag &= ~CONNECT_MQTT;
    // 这里可以处理断开连接后的逻辑，ESP-MQTT 底层通常会自动重连
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    // 打印收到的主题和数据内容
    // 注意：event->topic 和 event->data 不是以 '\0' 结尾的字符串，必须配合 len
    // 使用 %.*s 打印
    printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
    printf("DATA=%.*s\r\n", event->data_len, event->data);

    // --- 业务逻辑处理区 ---
    // 在这里，你可以根据 event->topic
    // 来判断是哪个主题下发的数据，并调用不同的处理函数
    if (strncmp(event->topic, TopicBuff[5], event->topic_len) == 0) {
      // 处理 OTA 通知
      Process_OTA_Inform(event->data, event->data_len);
    }
    break;

  case MQTT_EVENT_ERROR:
    ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
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
  // 确保 TopicBuff 已经被初始化
  MQTT_Init();

  // 配置 MQTT 客户端参数
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = broker_uri,       // 服务器地址
      .credentials.client_id = GW_DEVICENAME, // 客户端ID (DeviceName)
      .credentials.username = GW_PRODUCTID,   // 用户名 (ProductID)
      // 将刚才 MQTT_Init 中计算出来的鉴权密码传入
      .credentials.authentication.password = Mqtt_Password,
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
    ESP_LOGE(TAG, "MQTT client is not initialized");
    return -1;
  }
  // 调用底层 API 发布消息
  return esp_mqtt_client_publish(mqtt_client, topic, data, len, qos, retain);
}
