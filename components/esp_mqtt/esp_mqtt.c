/*-------------------------------------------------*/
/*                                                 */
/*       ESP-MQTT client integration for OneNET    */
/*                                                 */
/*-------------------------------------------------*/

#include "esp_mqtt.h"
#include "app_config.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "mw1268_app.h"
#include "utils_hmac.h"
#include "wifi_cat1.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef GW_DEVICENAME
#define GW_DEVICENAME "your_device_name"
#endif
#ifndef GW_PRODUCTID
#define GW_PRODUCTID "your_product_id"
#endif

static const char *TAG = "ESP_MQTT";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static uint32_t disconnect_start_tick = 0;
static TaskHandle_t ota_notify_task_handle = NULL;

#define MQTT_DISCONNECT_RESET_TIMEOUT_MS (180000)

extern Sys_CB SysCB;

char TopicBuff[10][128];
char TopicNum;
char Mqtt_Password[512];

static const char tempdata[8] = {'+', ' ', '/', '?', '%', '#', '&', '='};
static const char URLdata[8][4] = {"%2B", "%20", "%2F", "%3F",
                                   "%25", "%23", "%26", "%3D"};

static void ota_notify_check_task(void *pvParameters) {
  ESP_LOGI(TAG, "收到 ota/inform，准备触发 OTA 检查");
  vTaskDelay(pdMS_TO_TICKS(500));
  Studio_OTA_CheckTask();
  ota_notify_task_handle = NULL;
  vTaskDelete(NULL);
}

static void mqtt_schedule_ota_check_from_notify(void) {
  if (ota_notify_task_handle != NULL) {
    ESP_LOGW(TAG, "OTA 通知处理任务已在队列中，忽略重复触发");
    return;
  }

  BaseType_t ok = xTaskCreate(ota_notify_check_task, "ota_notify", 12288, NULL, 3,
                              &ota_notify_task_handle);
  if (ok != pdPASS) {
    ota_notify_task_handle = NULL;
    ESP_LOGE(TAG, "创建 ota_notify 任务失败");
  }
}

static void mqtt_publish_ota_notify_reply(const char *reply_id, int code,
                                          const char *msg) {
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "MQTT 客户端未初始化，无法回发 ota/inform_reply");
    return;
  }

  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGW(TAG, "MQTT 未连接，跳过 ota/inform_reply 回发");
    return;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return;
  }

  cJSON_AddStringToObject(root, "id", reply_id != NULL ? reply_id : "ota_notify");
  cJSON_AddNumberToObject(root, "code", code);
  cJSON_AddStringToObject(root, "msg", msg != NULL ? msg : "success");

  char *payload = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (payload == NULL) {
    return;
  }

  char topic[128];
  snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform_reply", GW_PRODUCTID,
           GW_DEVICENAME);
  int msg_id =
      esp_mqtt_client_publish(mqtt_client, topic, payload, strlen(payload), 1, 0);
  ESP_LOGI(TAG, "已发布 ota/inform_reply，msg_id=%d，payload=%s", msg_id, payload);
  free(payload);
}

static void mqtt_publish_property_state(const char *reply_id,
                                        const char *attribute, int value) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return;
  }

  cJSON_AddStringToObject(root, "id", reply_id);
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");
  cJSON_AddNumberToObject(params, attribute, value);

  char *reply_data = cJSON_PrintUnformatted(root);
  if (reply_data != NULL) {
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);
    esp_mqtt_publish_msg(topic, reply_data, strlen(reply_data), 1, 0);
    free(reply_data);
  }

  cJSON_Delete(root);
}

static int mqtt_get_numeric_value(cJSON *item, bool *ok) {
  if (ok != NULL) {
    *ok = false;
  }
  if (item == NULL) {
    return 0;
  }
  if (cJSON_IsNumber(item) || cJSON_IsBool(item)) {
    if (ok != NULL) {
      *ok = true;
    }
    return item->valueint;
  }
  if (cJSON_IsObject(item)) {
    cJSON *value = cJSON_GetObjectItem(item, "value");
    if (cJSON_IsNumber(value) || cJSON_IsBool(value)) {
      if (ok != NULL) {
        *ok = true;
      }
      return value->valueint;
    }
  }
  return 0;
}

static void mqtt_log_reply_code(const char *label, cJSON *root) {
  cJSON *code = cJSON_GetObjectItem(root, "code");
  cJSON *msg = cJSON_GetObjectItem(root, "msg");
  int code_value = cJSON_IsNumber(code) ? code->valueint : -1;

  if (code_value == 200 || code_value == 0) {
    ESP_LOGI(TAG, "%s success", label);
  } else {
    ESP_LOGE(TAG, "%s failed, code=%d, msg=%s", label, code_value,
             cJSON_IsString(msg) ? msg->valuestring : "unknown");
  }
}

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
      outdata[k++] = data[i];
    }
  }
  outdata[k] = '\0';
}

void MQTT_Init(void) {
  Token_CB token;
  memset(&token, 0, sizeof(token));

  int key_len =
      base64_decode(GW_DEVICESECRET, (unsigned char *)token.decodekey);
  snprintf(token.StringForSignature, sizeof(token.StringForSignature),
           "%s\nsha1\nproducts/%s/devices/%s\n2018-10-31", UNIX, GW_PRODUCTID,
           GW_DEVICENAME);

  utils_hmac_sha1_hex(token.StringForSignature,
                      strlen(token.StringForSignature), token.signtemp,
                      token.decodekey, key_len);
  base64_encode((unsigned char *)token.signtemp, token.sign, 20);

  snprintf(token.res, sizeof(token.res), "products/%s/devices/%s",
           GW_PRODUCTID, GW_DEVICENAME);
  URL_encode(token.sign, strlen(token.sign), token.signURL);
  URL_encode(token.res, strlen(token.res), token.resURL);

  snprintf(Mqtt_Password, sizeof(Mqtt_Password),
           "version=2018-10-31&res=%s&et=%s&method=sha1&sign=%s", token.resURL,
           UNIX, token.signURL);

  snprintf(TopicBuff[0], sizeof(TopicBuff[0]), "$sys/%s/%s/thing/property/set",
           GW_PRODUCTID, GW_DEVICENAME);
  snprintf(TopicBuff[1], sizeof(TopicBuff[1]),
           "$sys/%s/%s/thing/property/post/reply", GW_PRODUCTID,
           GW_DEVICENAME);
  snprintf(TopicBuff[2], sizeof(TopicBuff[2]),
           "$sys/%s/%s/thing/sub/login/reply", GW_PRODUCTID, GW_DEVICENAME);
  snprintf(TopicBuff[3], sizeof(TopicBuff[3]),
           "$sys/%s/%s/thing/sub/logout/reply", GW_PRODUCTID, GW_DEVICENAME);
  snprintf(TopicBuff[4], sizeof(TopicBuff[4]),
           "$sys/%s/%s/thing/sub/property/post/reply", GW_PRODUCTID,
           GW_DEVICENAME);
  snprintf(TopicBuff[5], sizeof(TopicBuff[5]),
           "$sys/%s/%s/thing/sub/property/get_reply", GW_PRODUCTID,
           GW_DEVICENAME);
  snprintf(TopicBuff[6], sizeof(TopicBuff[6]),
           "$sys/%s/%s/thing/pack/post/reply", GW_PRODUCTID, GW_DEVICENAME);
  snprintf(TopicBuff[7], sizeof(TopicBuff[7]),
           "$sys/%s/%s/thing/sub/property/set", GW_PRODUCTID, GW_DEVICENAME);
  snprintf(TopicBuff[8], sizeof(TopicBuff[8]), "$sys/%s/%s/ota/inform",
           GW_PRODUCTID, GW_DEVICENAME);

  TopicNum = 9;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT 已连接到 OneNET");
    SysCB.SysEventFlag |= CONNECT_MQTT;
    disconnect_start_tick = 0;

    for (int i = 0; i < TopicNum; i++) {
      int msg_id = esp_mqtt_client_subscribe(client, TopicBuff[i], 1);
      ESP_LOGD(TAG, "Subscribed topic=%s, msg_id=%d", TopicBuff[i], msg_id);
    }

    if ((SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
        !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
      ESP_LOGI(TAG, "LoRa 已确认连通，开始上报子设备上线");
      WiFi_Cat1_SubOnline(1, 1);
      SysCB.SysEventFlag |= SUB_ONLINE_READY;
    } else {
      ESP_LOGI(TAG, "等待 LoRa 确认后再上报子设备上线");
    }
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT 已断开连接");
    SysCB.SysEventFlag &= ~CONNECT_MQTT;
    SysCB.SysEventFlag &= ~SUB_ONLINE_READY;

    if (!(SysCB.SysEventFlag & OTA_RUNNING)) {
      if (disconnect_start_tick == 0) {
        disconnect_start_tick = xTaskGetTickCount();
      } else {
        uint32_t diff =
            (xTaskGetTickCount() - disconnect_start_tick) * portTICK_PERIOD_MS;
        if (diff > MQTT_DISCONNECT_RESET_TIMEOUT_MS) {
          ESP_LOGE(TAG, "MQTT 断开时间过长，重置 CAT1 模块");
          disconnect_start_tick = 0;
          Cat1_Reset();
        }
      }
    }
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGD(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_UNSUBSCRIBED:
    ESP_LOGD(TAG, "MQTT unsubscribed, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_PUBLISHED:
    ESP_LOGD(TAG, "MQTT published, msg_id=%d", event->msg_id);
    break;

  case MQTT_EVENT_DATA: {
    char topic_tmp[128] = {0};
    int copy_len = event->topic_len < (int)(sizeof(topic_tmp) - 1)
                       ? event->topic_len
                       : (int)(sizeof(topic_tmp) - 1);
    memcpy(topic_tmp, event->topic, copy_len);
    topic_tmp[copy_len] = '\0';

    ESP_LOGD(TAG, "MQTT topic: %s", topic_tmp);
    ESP_LOGD(TAG, "MQTT payload: %.*s", event->data_len, event->data);

    if (strstr(topic_tmp, "thing/sub/login/reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        mqtt_log_reply_code("sub-device login", root);
        cJSON_Delete(root);
      }
    }

    if (strstr(topic_tmp, "thing/sub/property/post/reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        mqtt_log_reply_code("sub-device property post", root);
        cJSON_Delete(root);
      }
    }

    if (strstr(topic_tmp, "thing/sub/property/get_reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        cJSON *code = cJSON_GetObjectItem(root, "code");
        if (cJSON_IsNumber(code) && (code->valueint == 200 || code->valueint == 0)) {
          cJSON *data = cJSON_GetObjectItem(root, "data");
          char *data_str = data ? cJSON_PrintUnformatted(data) : NULL;
          ESP_LOGI(TAG, "Sub-device latest properties: %s",
                   data_str ? data_str : "{}");
          free(data_str);
        } else {
          mqtt_log_reply_code("sub-device property get", root);
        }
        cJSON_Delete(root);
      }
    }

    if (strstr(topic_tmp, "thing/pack/post/reply")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        mqtt_log_reply_code("pack post", root);
        cJSON_Delete(root);
      }
    }

    if (strstr(topic_tmp, "/ota/inform")) {
      char reply_id[32] = {0};
      snprintf(reply_id, sizeof(reply_id), "%lu",
               (unsigned long)xTaskGetTickCount());

      ESP_LOGI(TAG, "收到 ota/inform 原始载荷: %.*s", event->data_len,
               event->data);
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        cJSON *msg_id_obj = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsString(msg_id_obj) && msg_id_obj->valuestring != NULL &&
            msg_id_obj->valuestring[0] != '\0') {
          snprintf(reply_id, sizeof(reply_id), "%s", msg_id_obj->valuestring);
        }
        cJSON_Delete(root);
      } else {
        ESP_LOGW(TAG, "ota/inform 载荷不是合法的 JSON，继续按默认 id 回包");
      }

      mqtt_publish_ota_notify_reply(reply_id, 200, "success");
      mqtt_schedule_ota_check_from_notify();
    }

    if (strstr(topic_tmp, "thing/property/set") ||
        strstr(topic_tmp, "thing/sub/property/set")) {
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        char reply_id[32] = "20240513";
        cJSON *msg_id_obj = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsString(msg_id_obj) && msg_id_obj->valuestring != NULL) {
          snprintf(reply_id, sizeof(reply_id), "%s", msg_id_obj->valuestring);
        }

        cJSON *params = cJSON_GetObjectItem(root, "params");
        cJSON *sub_params = params ? cJSON_GetObjectItem(params, "params") : NULL;
        cJSON *target_params = sub_params ? sub_params : params;

        if (target_params != NULL) {
          bool ok = false;
          int pest_value =
              mqtt_get_numeric_value(cJSON_GetObjectItem(target_params, ATTRIBUTE1),
                                     &ok);
          if (ok) {
            ESP_LOGI(TAG, "Set gateway PestAlarm=%d", pest_value);
            gpio_set_level(LED_GW001_LED_PIN, pest_value ? 0 : 1);
            mqtt_publish_property_state(reply_id, ATTRIBUTE1, pest_value);
          }

          int node_led_value = mqtt_get_numeric_value(
              cJSON_GetObjectItem(target_params, ATTRIBUTE2), &ok);
          if (ok) {
            ESP_LOGI(TAG, "Set sub-node LED=%d", node_led_value);
            LoRa_ControlNodeLED(node_led_value);
            mqtt_publish_property_state(reply_id, ATTRIBUTE2, node_led_value);
          }
        }

        cJSON_Delete(root);
      }
    }
    break;
  }

  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT transport error");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      ESP_LOGE(TAG, "esp-tls err=0x%x, tls stack=0x%x, sock errno=%d (%s)",
               event->error_handle->esp_tls_last_esp_err,
               event->error_handle->esp_tls_stack_err,
               event->error_handle->esp_transport_sock_errno,
               strerror(event->error_handle->esp_transport_sock_errno));
    } else if (event->error_handle->error_type ==
               MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
      ESP_LOGE(TAG, "Connection refused, code=0x%x",
               event->error_handle->connect_return_code);
    } else {
      ESP_LOGE(TAG, "Other MQTT error type=%d",
               event->error_handle->error_type);
    }
    break;

  default:
    ESP_LOGD(TAG, "Unhandled MQTT event id=%d", event->event_id);
    break;
  }
}

esp_err_t esp_mqtt_app_start(const char *broker_uri) {
  if (mqtt_client != NULL) {
    ESP_LOGW(TAG, "MQTT client already initialized, skip duplicate start");
    return ESP_OK;
  }

  MQTT_Init();

  char target_uri[128];
  if (broker_uri != NULL && broker_uri[0] != '\0') {
    snprintf(target_uri, sizeof(target_uri), "%s", broker_uri);
  } else {
    snprintf(target_uri, sizeof(target_uri), "mqtt://%s:%d", MQTT_SERVER,
             MQTT_PORT);
  }

  ESP_LOGI(TAG, "正在连接 MQTT: %s", target_uri);

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = target_uri,
      .credentials.client_id = GW_DEVICENAME,
      .credentials.username = GW_PRODUCTID,
      .credentials.authentication.password = Mqtt_Password,
      .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
      .session.keepalive = 120,
      .network.timeout_ms = 20000,
      .network.reconnect_timeout_ms = 5000,
      .buffer.size = 8192,
      .buffer.out_size = 8192,
      .task.stack_size = 8192,
  };

  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize MQTT client");
    return ESP_FAIL;
  }

  esp_err_t err = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                                 mqtt_event_handler, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register MQTT event: %s", esp_err_to_name(err));
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    return err;
  }

  err = esp_mqtt_client_start(mqtt_client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    return err;
  }

  ESP_LOGI(TAG, "MQTT 客户端已启动");
  return ESP_OK;
}

void esp_mqtt_app_stop(void) {
  if (mqtt_client != NULL) {
    esp_mqtt_client_stop(mqtt_client);
    esp_mqtt_client_destroy(mqtt_client);
    mqtt_client = NULL;
    ESP_LOGI(TAG, "MQTT client stopped");
  }
}

int esp_mqtt_publish_msg(const char *topic, const char *data, int len, int qos,
                         int retain) {
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "MQTT client is not initialized");
    return -1;
  }

  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGW(TAG, "MQTT not connected, drop topic=%s", topic);
    return -3;
  }

  if (SysCB.SysEventFlag & OTA_RUNNING) {
    ESP_LOGW(TAG, "OTA running, block publish topic=%s", topic);
    return -2;
  }

  int msg_id = esp_mqtt_client_publish(mqtt_client, topic, data, len, qos,
                                       retain);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "MQTT publish failed, topic=%s, err=%d", topic, msg_id);
  }
  return msg_id;
}
