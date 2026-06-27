/*-------------------------------------------------*/
/*                                                 */
/*       ESP-MQTT client integration for OneNET    */
/*                                                 */
/*-------------------------------------------------*/

#include "esp_mqtt.h"
#include "mem_guard.h"
#include "app_config.h"
#include "bsp_led.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "mw1268_app.h"
#include "utils_hmac.h"
#include "wifi_cat1.h"
#include "bsp_storage.h"
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
static const char *OTA_TAG = "OTA_MQTT";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static uint32_t disconnect_start_tick = 0;
static TaskHandle_t ota_notify_task_handle = NULL;
static TaskHandle_t sub_login_retry_task_handle = NULL;
static int ota_inform_sub_msg_id = -1;

#define MQTT_DISCONNECT_RESET_TIMEOUT_MS (180000)
#define SUB_LOGIN_RETRY_DELAY_MS (5000)
#define OTA_NOTIFY_REBOOT_DELAY_MS (1500)

extern Sys_CB SysCB;

char TopicBuff[10][128];
char TopicNum;
char Mqtt_Password[512];

static const char tempdata[8] = {'+', ' ', '/', '?', '%', '#', '&', '='};
static const char URLdata[8][4] = {"%2B", "%20", "%2F", "%3F",
                                   "%25", "%23", "%26", "%3D"};

static void ota_notify_check_task(void *pvParameters) {
  ESP_LOGI(OTA_TAG, "�յ� ota/inform��׼������ OTA ���");
  vTaskDelay(pdMS_TO_TICKS(500));
  OneNET_FuseOTA_CheckTask();
  ota_notify_task_handle = NULL;
  vTaskDelete(NULL);
}

static void __attribute__((unused)) mqtt_schedule_ota_check_from_notify(void) {
  if (ota_notify_task_handle != NULL) {
    ESP_LOGW(OTA_TAG, "OTA ֪ͨ�����������ڶ����У������ظ�����");
    return;
  }

  BaseType_t ok = xTaskCreate(ota_notify_check_task, "ota_notify", 12288, NULL, 3,
                              &ota_notify_task_handle);
  if (ok != pdPASS) {
    ota_notify_task_handle = NULL;
    ESP_LOGE(OTA_TAG, "���� ota_notify ����ʧ��");
  } else {
    ESP_LOGI(OTA_TAG, "�ѵ��� OTA �������");
  }
}

static void ota_notify_reboot_task(void *pvParameters) {
  (void)pvParameters;

  ESP_LOGI(OTA_TAG, "�յ� ota/inform��׼���������ͳһ OTA �������");
  WiFi_Cat1_RequestOtaNotifyReboot();
  vTaskDelay(pdMS_TO_TICKS(OTA_NOTIFY_REBOOT_DELAY_MS));
  ESP_LOGI(OTA_TAG, "��������������ͳһִ�� OTA bootstrap");
  ota_notify_task_handle = NULL;
  esp_restart();
}

static void mqtt_schedule_ota_reboot_from_notify(void) {
  if (WiFi_Cat1_IsOtaNotifyBootstrapPendingOrActive()) {
    ESP_LOGW(OTA_TAG, "OTA ���������ѹ�������������·�У������ظ�֪ͨ");
    return;
  }

  if (ota_notify_task_handle != NULL) {
    ESP_LOGW(OTA_TAG, "OTA ֪ͨ�����������ڶ����У������ظ�����");
    return;
  }

  BaseType_t ok = xTaskCreate(ota_notify_reboot_task, "ota_notify", 4096, NULL,
                              3, &ota_notify_task_handle);
  if (ok != pdPASS) {
    ota_notify_task_handle = NULL;
    ESP_LOGE(OTA_TAG, "���� ota_notify ��������ʧ��");
  } else {
    ESP_LOGI(OTA_TAG, "�ѵ��� OTA ֪ͨ��������");
  }
}

static bool mqtt_reply_code_is_success(cJSON *root) {
  cJSON *code = cJSON_GetObjectItem(root, "code");
  int code_value = cJSON_IsNumber(code) ? code->valueint : -1;
  return code_value == 200 || code_value == 0;
}

static void sub_login_retry_task(void *pvParameters) {
  uint32_t delay_ms = (uint32_t)(uintptr_t)pvParameters;
  while ((SysCB.SysEventFlag & CONNECT_MQTT) &&
         (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
         !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
    if (delay_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    if (!(SysCB.SysEventFlag & CONNECT_MQTT) ||
        !(SysCB.SysEventFlag & SUB_LORA_CONFIRMED) ||
        (SysCB.SysEventFlag & SUB_ONLINE_READY)) {
      break;
    }

    ESP_LOGW(TAG, "Retrying sub-device login after previous failure");
    if (WiFi_Cat1_SubOnline(1, 1) == ESP_OK) {
      break;
    }
    ESP_LOGW(TAG, "Sub-device login retry publish failed");
    delay_ms = SUB_LOGIN_RETRY_DELAY_MS;
  }

  sub_login_retry_task_handle = NULL;
  vTaskDelete(NULL);
}

static void mqtt_schedule_sub_login_retry(uint32_t delay_ms) {
  if (sub_login_retry_task_handle != NULL) {
    ESP_LOGW(TAG, "Sub-device login retry already scheduled");
    return;
  }

  BaseType_t ok =
      xTaskCreate(sub_login_retry_task, "sub_login_retry", 4096,
                  (void *)(uintptr_t)delay_ms, 4, &sub_login_retry_task_handle);
  if (ok != pdPASS) {
    sub_login_retry_task_handle = NULL;
    ESP_LOGE(TAG, "Failed to create sub-device login retry task");
  }
}

static void mqtt_try_subdevice_login_now(const char *reason) {
  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    return;
  }
  if (!(SysCB.SysEventFlag & SUB_LORA_CONFIRMED)) {
    ESP_LOGI(TAG, "Waiting for LoRa confirmation before sub-device login (%s)",
             reason);
    return;
  }
  if (SysCB.SysEventFlag & SUB_ONLINE_READY) {
    ESP_LOGI(TAG, "Sub-device already online, skip redundant login (%s)", reason);
    return;
  }

  esp_err_t err = WiFi_Cat1_SubOnline(1, 1);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Sub-device login sent (%s)", reason);
  } else {
    ESP_LOGW(TAG, "Sub-device login publish failed (%s): %s", reason,
             esp_err_to_name(err));
    mqtt_schedule_sub_login_retry(SUB_LOGIN_RETRY_DELAY_MS);
  }
}

static void mqtt_publish_ota_notify_reply(const char *reply_id, int code,
                                          const char *msg) {
  if (mqtt_client == NULL) {
    ESP_LOGE(OTA_TAG, "MQTT �ͻ���δ��ʼ�����޷��ط� ota/inform_reply");
    return;
  }

  if (!(SysCB.SysEventFlag & CONNECT_MQTT)) {
    ESP_LOGW(OTA_TAG, "MQTT δ���ӣ����� ota/inform_reply �ط�");
    return;
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return;
  }

  cJSON_AddStringToObject(root, "id", reply_id != NULL ? reply_id : "ota_notify");
  cJSON_AddNumberToObject(root, "code", code);
  cJSON_AddStringToObject(root, "msg", msg != NULL ? msg : "success");

  static char payload_buf[512];
  if (!cJSON_PrintPreallocated(root, payload_buf, sizeof(payload_buf), 0)) {
    cJSON_Delete(root);
    ESP_LOGE(OTA_TAG, "ota/inform_reply payload overflow");
    return;
  }
  char *payload = payload_buf;

  char topic[128];
  snprintf(topic, sizeof(topic), "$sys/%s/%s/ota/inform_reply", GW_PRODUCTID,
           GW_DEVICENAME);
  int msg_id =
      esp_mqtt_client_publish(mqtt_client, topic, payload, strlen(payload), 1, 0);
  ESP_LOGI(OTA_TAG, "�ѷ��� ota/inform_reply��msg_id=%d��payload=%s", msg_id,
           payload);
  cJSON_Delete(root);
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

  static char reply_buf[512];
  if (cJSON_PrintPreallocated(root, reply_buf, sizeof(reply_buf), 0)) {
    char *reply_data = reply_buf;
    char topic[128];
    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);
    esp_mqtt_publish_msg(topic, reply_data, strlen(reply_data), 1, 0);
  } else {
    ESP_LOGE(TAG, "property/post reply payload overflow");
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
    ESP_LOGI(TAG, "MQTT �����ӵ� OneNET");
    SysCB.SysEventFlag |= CONNECT_MQTT;
    SysCB.SysEventFlag &= ~SUB_ONLINE_READY;
    disconnect_start_tick = 0;
    ota_inform_sub_msg_id = -1;

    if (!mem_guard_mqtt_sub_blocked()) {
    for (int i = 0; i < TopicNum; i++) {
      int msg_id = esp_mqtt_client_subscribe(client, TopicBuff[i], 1);
      ESP_LOGD(TAG, "Subscribed topic=%s, msg_id=%d", TopicBuff[i], msg_id);
      if (i == 8) {
        ota_inform_sub_msg_id = msg_id;
        ESP_LOGI(OTA_TAG, "���� OTA ֪ͨ����: %s, msg_id=%d", TopicBuff[i], msg_id);
      }
    }
    } else {
      ESP_LOGE(TAG, "MQTT subscribe blocked: low memory (level %d)", (int)mem_guard_get_level());
    }

    if ((SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
        !(SysCB.SysEventFlag & SUB_ONLINE_READY)) {
      ESP_LOGI(TAG, "LoRa ��ȷ����ͨ����ʼ�ϱ����豸����");
      mqtt_try_subdevice_login_now("mqtt connected");
    } else {
      ESP_LOGI(TAG, "�ȴ� LoRa ȷ�Ϻ����ϱ����豸����");
    }
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT �ѶϿ�����");
    SysCB.SysEventFlag &= ~CONNECT_MQTT;
    SysCB.SysEventFlag &= ~SUB_ONLINE_READY;

    if (!(SysCB.SysEventFlag & OTA_RUNNING)) {
      if (disconnect_start_tick == 0) {
        disconnect_start_tick = xTaskGetTickCount();
      } else {
        uint32_t diff =
            (xTaskGetTickCount() - disconnect_start_tick) * portTICK_PERIOD_MS;
        if (diff > MQTT_DISCONNECT_RESET_TIMEOUT_MS) {
          ESP_LOGE(TAG, "MQTT �Ͽ�ʱ����������� CAT1 ģ��");
          disconnect_start_tick = 0;
          Cat1_Reset();
        }
      }
    }
    break;

  case MQTT_EVENT_SUBSCRIBED:
    ESP_LOGD(TAG, "MQTT subscribed, msg_id=%d", event->msg_id);
    if (event->msg_id == ota_inform_sub_msg_id) {
      ESP_LOGI(OTA_TAG, "OTA ֪ͨ���ⶩ�ĳɹ�, msg_id=%d", event->msg_id);
    }
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
      ESP_LOGI(TAG, "sub-device login reply payload: %.*s", event->data_len,
               event->data);
      cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
      if (root != NULL) {
        if (mqtt_reply_code_is_success(root)) {
          SysCB.SysEventFlag |= SUB_ONLINE_READY;
          ESP_LOGI(TAG, "sub-device login success, marked online");
        } else {
          SysCB.SysEventFlag &= ~SUB_ONLINE_READY;
          mqtt_log_reply_code("sub-device login", root);
          mqtt_schedule_sub_login_retry(SUB_LOGIN_RETRY_DELAY_MS);
        }
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
          static char data_str_buf[512];
          char *data_str = NULL;
          if (data && cJSON_PrintPreallocated(data, data_str_buf, sizeof(data_str_buf), 0)) {
            data_str = data_str_buf;
          }
          ESP_LOGI(TAG, "Sub-device latest properties: %s",
                   data_str ? data_str : "{}");
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

      ESP_LOGI(OTA_TAG, "�յ� ota/inform ԭʼ�غ�: %.*s", event->data_len,
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
        ESP_LOGW(OTA_TAG, "ota/inform �غɲ��ǺϷ��� JSON��������Ĭ�� id �ذ�");
      }

      mqtt_publish_ota_notify_reply(reply_id, 200, "success");
      mqtt_schedule_ota_reboot_from_notify();
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
            esp_err_t led_err = bsp_led_set(pest_value != 0);
            if (led_err != ESP_OK) {
              ESP_LOGE(TAG, "Failed to set onboard RGB: %s",
                       esp_err_to_name(led_err));
            }
            mqtt_publish_property_state(reply_id, ATTRIBUTE1, pest_value);
          }

          int node_led_value = mqtt_get_numeric_value(
              cJSON_GetObjectItem(target_params, ATTRIBUTE2), &ok);
          if (ok) {
            ESP_LOGI(TAG, "Set sub-node LED=%d", node_led_value);
            LoRa_ControlNodeLED(node_led_value);
            mqtt_publish_property_state(reply_id, ATTRIBUTE2, node_led_value);

          /* Factory reset command */
          int factory_rst_val = mqtt_get_numeric_value(
              cJSON_GetObjectItem(target_params, ATTRIBUTE_FACTORY_RESET), &ok);
          if (ok && factory_rst_val == 1) {
            ESP_LOGW(TAG, "Factory reset command received via MQTT!");
            mqtt_publish_property_state(reply_id, ATTRIBUTE_FACTORY_RESET, 0);
            factory_reset_set_pending();
            vTaskDelay(pdMS_TO_TICKS(2000));
            factory_reset();
          }
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

  ESP_LOGI(TAG, "�������� MQTT: %s", target_uri);

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

  ESP_LOGI(TAG, "MQTT �ͻ��������");
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
