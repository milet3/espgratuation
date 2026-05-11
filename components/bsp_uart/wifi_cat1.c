#include "wifi_cat1.h"
#include "app_config.h"
#include "bsp_uart.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mqtt.h"
#include "esp_timer.h"
// #include "esp_wifi.h" // 补充缺失的头文件
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "stdio.h"
#include "string.h"

Pack_CB pack;
static const char *TAG = "WIFI_CAT1";
QueueHandle_t OTA_ZC_Queue = NULL;

void WiFi_Cat1_SubOnline(char sub_num, char mode) {
  char temptopic[128];
  char tempdata[256];

  memset(temptopic, 0, sizeof(temptopic));
  memset(tempdata, 0, sizeof(tempdata));

  if (mode == 0) {
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/logout",
             GW_PRODUCTID, GW_DEVICENAME);
  } else {
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/login",
             GW_PRODUCTID, GW_DEVICENAME);
  }

  snprintf(tempdata, sizeof(tempdata),
           "{\"id\": \"%d\",\"version\": \"1.0\",\"params\": {\"productID\": "
           "\"%s\", \"deviceName\": \"%s\"}}",
           sub_num, SUB_PRODUCTID, DeviceNameBuff[(int)sub_num]);

  esp_mqtt_publish_msg(temptopic, tempdata, strlen(tempdata), 0, 0);
  ESP_LOGI(TAG, "SubOnline sent: %s", tempdata);
}

void WiFi_Cat1_SubDataPost(unsigned char *postdata) {
  char temptopic[64];

  memset(temptopic, 0, sizeof(temptopic));
  snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/pack/post",
           GW_PRODUCTID, GW_DEVICENAME);

  esp_mqtt_publish_msg(temptopic, (const char *)postdata,
                       strlen((char *)postdata), 0, 0);
  ESP_LOGI(TAG, "SubDataPost sent: %s", postdata);
}

void WiFi_Cat1_GatewayDataPost(float temp, float hum, float lux) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  cJSON_AddStringToObject(root, "id", "123");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 1. 空气温度
  cJSON *temp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE5);
  cJSON_AddNumberToObject(temp_obj, "value", temp);

  // 2. 空气湿度
  cJSON *hum_obj = cJSON_AddObjectToObject(params, ATTRIBUTE6);
  cJSON_AddNumberToObject(hum_obj, "value", hum);

  // 3. 光照强度
  cJSON *lux_obj = cJSON_AddObjectToObject(params, ATTRIBUTE7);
  cJSON_AddNumberToObject(lux_obj, "value", lux);

  // 4. 【核心】固件版本属性上报 (新版 OneNET Studio OTA 关键)
  // 必须与控制台定义的标识符 "firmware_version" 完全一致
  cJSON *ver_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_FIRMWARE_VER);
  cJSON_AddStringToObject(ver_obj, "value", GATEWAY_VERSION);

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    esp_mqtt_publish_msg(temptopic, post_data, strlen(post_data), 0, 0);
    ESP_LOGI(TAG, "GatewayDataPost (含版本号) 已发送: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_SoilDataPost(float temp, float humi, float ec, float n, float p,
                            float k) {
  char temptopic[128];
  char tempdata[512];

  // OneNet 物模型属性上报主题
  snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
           GW_PRODUCTID, GW_DEVICENAME);

  // 构造 JSON 数据包
  // 使用 app_config.h 中定义的土壤标识符
  // 注意：根据平台显示，EC, N, P, K 为 int32 类型，因此使用 %.0f (或强制转int)
  // 以免带小数点导致平台解析失败
  snprintf(tempdata, sizeof(tempdata),
           "{\"id\":\"456\",\"version\":\"1.0\",\"params\":{"
           "\"%s\":{\"value\":%.2f}," // 温度 (double)
           "\"%s\":{\"value\":%.2f}," // 水分 (double)
           "\"%s\":{\"value\":%.0f}," // EC (int32)
           "\"%s\":{\"value\":%.0f}," // 氮 (int32)
           "\"%s\":{\"value\":%.0f}," // 磷 (int32)
           "\"%s\":{\"value\":%.0f}"  // 钾 (int32)
           "}}",
           ATTRIBUTE_SOIL_TEMP, temp, ATTRIBUTE_SOIL_HUMI, humi,
           ATTRIBUTE_SOIL_EC, ec, ATTRIBUTE_SOIL_N, n, ATTRIBUTE_SOIL_P, p,
           ATTRIBUTE_SOIL_K, k);

  esp_mqtt_publish_msg(temptopic, tempdata, strlen(tempdata), 0, 0);
  ESP_LOGI(TAG, "SoilDataPost sent to %s: %s", temptopic, tempdata);
}

void WiFi_Cat1_AdcDataPost(float adc1, float adc2, float adc3) {
  char temptopic[128];
  char tempdata[512];

  // OneNet 物模型属性上报主题
  snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
           GW_PRODUCTID, GW_DEVICENAME);

  // 构造 JSON 数据包
  // 使用 app_config.h 中定义的 ADC 标识符 (ATTRIBUTE8, 9, 10)
  snprintf(tempdata, sizeof(tempdata),
           "{\"id\":\"789\",\"version\":\"1.0\",\"params\":{"
           "\"%s\":{\"value\":%.2f}," // ADC1
           "\"%s\":{\"value\":%.2f}," // ADC2
           "\"%s\":{\"value\":%.2f}"  // ADC3
           "}}",
           ATTRIBUTE8, adc1, ATTRIBUTE9, adc2, ATTRIBUTE10, adc3);

  esp_mqtt_publish_msg(temptopic, tempdata, strlen(tempdata), 0, 0);
  ESP_LOGI(TAG, "AdcDataPost sent to %s: %s", temptopic, tempdata);
}

void WiFi_Cat1_AllDataPost(float air_temp, float air_hum, float air_lux,
                           float soil_temp, float soil_humi, float soil_ec,
                           float soil_n, float soil_p, float soil_k, float adc1,
                           float adc2, float adc3) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  cJSON_AddStringToObject(root, "id", "999");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 1. 网关空气温湿度 + 光强
  cJSON *temp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE5);
  cJSON_AddNumberToObject(temp_obj, "value", air_temp);
  cJSON *hum_obj = cJSON_AddObjectToObject(params, ATTRIBUTE6);
  cJSON_AddNumberToObject(hum_obj, "value", air_hum);
  cJSON *lux_obj = cJSON_AddObjectToObject(params, ATTRIBUTE7);
  cJSON_AddNumberToObject(lux_obj, "value", air_lux);

  // 2. 土壤传感器数据
  cJSON *stemp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_TEMP);
  cJSON_AddNumberToObject(stemp_obj, "value", soil_temp);
  cJSON *shum_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_HUMI);
  cJSON_AddNumberToObject(shum_obj, "value", soil_humi);
  cJSON *sec_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_EC);
  cJSON_AddNumberToObject(sec_obj, "value", (int)soil_ec);
  cJSON *sn_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_N);
  cJSON_AddNumberToObject(sn_obj, "value", (int)soil_n);
  cJSON *sp_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_P);
  cJSON_AddNumberToObject(sp_obj, "value", (int)soil_p);
  cJSON *sk_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_K);
  cJSON_AddNumberToObject(sk_obj, "value", (int)soil_k);

  // 3. ADC 数据
  cJSON *adc1_obj = cJSON_AddObjectToObject(params, ATTRIBUTE8);
  cJSON_AddNumberToObject(adc1_obj, "value", adc1);
  cJSON *adc2_obj = cJSON_AddObjectToObject(params, ATTRIBUTE9);
  cJSON_AddNumberToObject(adc2_obj, "value", adc2);
  cJSON *adc3_obj = cJSON_AddObjectToObject(params, ATTRIBUTE10);
  cJSON_AddNumberToObject(adc3_obj, "value", adc3);

  // 4. 固件版本号 (关键：用于 OneNET 识别设备版本)
  cJSON *ver_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_FIRMWARE_VER);
  cJSON_AddStringToObject(ver_obj, "value", GATEWAY_VERSION);

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data) {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    esp_mqtt_publish_msg(temptopic, post_data, strlen(post_data), 0, 0);
    ESP_LOGI(TAG, "AllDataPost (合并上报) 已发送: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

/*-------------------------------------------------*/
/*函数名：复位4G Cat1模块                          */
/*参  数：无                                       */
/*返回值：无                                       */
/*-------------------------------------------------*/
void WiFi_Cat1_InitGPIO(void) {
#if (CAT1_POWER_PIN >= 0)
  gpio_set_direction(CAT1_POWER_PIN, GPIO_MODE_OUTPUT);
#endif
#if (CAT1_POWER_STA_PIN >= 0)
  gpio_set_direction(CAT1_POWER_STA_PIN, GPIO_MODE_INPUT);
#endif
#if (CAT1_NET_STA_PIN >= 0)
  gpio_set_direction(CAT1_NET_STA_PIN, GPIO_MODE_INPUT);
#endif
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

/*
void WiFi_Reset(void) {
  esp_wifi_stop();
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_wifi_start();
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_wifi_connect();
}

void wifi_full_reset() {
  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  esp_wifi_connect();
}
*/

void WiFi_Cat1_ReportVersion(const char *id) {
  // ==========================================================
  // 核心变更：新版 OneNET 物联网开放平台 (Studio) 规范
  // ==========================================================
  if (id != NULL) {
    char reply_topic[128];
    char reply_data[128];
    snprintf(reply_topic, sizeof(reply_topic), "$sys/%s/%s/ota/inform_reply",
             GW_PRODUCTID, GW_DEVICENAME);

    // 修正：确保 ID 类型正确且包含 msg 描述
    snprintf(reply_data, sizeof(reply_data),
             "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", id);

    esp_mqtt_publish_msg(reply_topic, reply_data, strlen(reply_data), 1, 0);
    ESP_LOGI(TAG, "已向平台发送 OTA 指令确认 (ACK): %s", reply_data);
  } else {
    ESP_LOGI(
        TAG,
        "版本号将通过 GatewayDataPost 随属性周期性上报，无需在此单独报备。");
  }
}

void WiFi_Cat1_PropertyVersion(uint8_t num) {
  ESP_LOGI(TAG, "WiFi_Cat1_PropertyVersion %d", num);
}

void WiFi_Cat1_CheckOTATask(uint8_t num) {
  ESP_LOGI(TAG, "WiFi_Cat1_CheckOTATask %d", num);
}

typedef struct {
  char url[1024];
  char token[256]; // 新增：固件下载 Token
  uint8_t ota_staflag;
} ota_task_args_t;

static void ota_download_task(void *pvParameters) {
  ota_task_args_t *args = (ota_task_args_t *)pvParameters;
  ESP_LOGI(TAG, "开始从 %s 下载 OTA 固件", args->url);

  esp_http_client_config_t config = {
      .url = args->url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 30000, // 增加到 30 秒，适应大文件下载
      .crt_bundle_attach = esp_crt_bundle_attach, // 支持 HTTPS
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "HTTP 客户端初始化失败");
    SysCB.SysEventFlag &= ~OTA_RUNNING;
    free(args);
    vTaskDelete(NULL);
    return;
  }

  // 【修正】新版 OneNET Studio 规范：
  // 下载 URL 已经包含了 Token (download/ota_xxxx)，
  // 绝对不能在 Header 里再加 Authorization，否则会导致鉴权失败。
  // 直接发起 GET 请求即可。

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "无法打开 HTTP 连接: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    SysCB.SysEventFlag &= ~OTA_RUNNING;
    free(args);
    vTaskDelete(NULL);
    return;
  }
  // ... 后续逻辑
  int content_length = esp_http_client_fetch_headers(client);
  if (content_length <= 0) {
    ESP_LOGE(TAG, "获取固件大小失败");
    esp_http_client_cleanup(client);
    SysCB.SysEventFlag &= ~OTA_RUNNING; // 释放标志
    free(args);
    vTaskDelete(NULL);
    return;
  }

  uint8_t *buffer = malloc(OTA_RANGE_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "内存分配失败");
    esp_http_client_cleanup(client);
    SysCB.SysEventFlag &= ~OTA_RUNNING; // 释放标志
    free(args);
    vTaskDelete(NULL);
    return;
  }

  int total_read = 0;
  uint32_t page_index = 0;

  while (total_read < content_length) {
    int read = esp_http_client_read(client, (char *)buffer, OTA_RANGE_SIZE);
    if (read < 0) {
      ESP_LOGE(TAG, "HTTP 读取错误");
      break;
    } else if (read == 0) {
      break;
    }
    total_read += read;
    uint8_t is_last = (total_read >= content_length) ? 1 : 0;

    // 调用现有的处理逻辑，将数据块放入队列
    OTAServer_process(buffer, read, page_index, is_last, args->ota_staflag);

    page_index++;
    // 给其他任务一点运行时间，防止看门狗复位
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  ESP_LOGI(TAG, "OTA 下载完成，总共读取: %d 字节", total_read);

  // 清除 OTA 运行标志，允许其他任务恢复
  SysCB.SysEventFlag &= ~OTA_RUNNING;

  free(buffer);
  esp_http_client_cleanup(client);
  free(args);
  vTaskDelete(NULL);
}

void WiFi_Cat1_StartOTA(const char *url, const char *token,
                        uint8_t ota_staflag) {
  // 同步设置 OTA 运行标志，确保立即拦截其他任务的 MQTT 发送请求
  SysCB.SysEventFlag |= OTA_RUNNING;

  ota_task_args_t *args = malloc(sizeof(ota_task_args_t));
  if (args == NULL) {
    SysCB.SysEventFlag &= ~OTA_RUNNING; // 分配失败需恢复标志
    return;
  }

  memset(args, 0, sizeof(ota_task_args_t));
  strncpy(args->url, url, sizeof(args->url) - 1);
  if (token) {
    strncpy(args->token, token, sizeof(args->token) - 1);
  }
  args->ota_staflag = ota_staflag;

  xTaskCreate(ota_download_task, "ota_download", 8192, args, 5, NULL);
}

/**
 * @brief 适配新版 OneNET Studio 的 OTA 检查逻辑
 */
void Studio_OTA_CheckTask(void) {
  extern char Mqtt_Password[]; // 复用 MQTT 连接的 Token 进行鉴权

  // 1. 拼接新版 Studio OTA 检查接口 URL
  // 注意域名是 studio-ota.heclouds.com
  char url[512];
  snprintf(url, sizeof(url),
           "https://studio-ota.heclouds.com/ota/south/check?product_id=%s&"
           "device_name=%s&version=%s",
           GW_PRODUCTID, GW_DEVICENAME, CURRENT_FW_VERSION);

  ESP_LOGI(TAG, "正在请求 Studio OTA 接口: %s", url);

  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .timeout_ms = 15000,
      // 核心修改：跳过证书验证，解决 PPP 环境下的 TLS 握手超时问题
      .skip_cert_common_name_check = true,
      .use_global_ca_store = false,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client)
    return;

  // 2. 设置鉴权头（使用 MQTT 的 Token）
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
          ESP_LOGI(TAG, "Studio OTA 返回数据: %s", buffer);

          // 3. 解析 JSON，提取下载 URL
          cJSON *root = cJSON_Parse(buffer);
          if (root) {
            // 新版 API 标准返回格式类似: {"code":0, "msg":"succ",
            // "data":{"url":"http://...", "task_id":"..."}}
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data && cJSON_IsObject(data)) {
              cJSON *url_item = cJSON_GetObjectItem(data, "url");

              if (url_item && cJSON_IsString(url_item)) {
                const char *download_url = url_item->valuestring;
                ESP_LOGI(TAG, ">>> 成功获取固件下载链接: %s", download_url);

                // 4. 启动您原有的下载任务
                WiFi_Cat1_StartOTA(download_url, NULL, 0);
              } else {
                ESP_LOGW(TAG, "!!! JSON 中未找到 url "
                              "字段，可能当前版本无升级任务 !!!");
              }
            }
            cJSON_Delete(root);
          }
        }
        free(buffer);
      }
    } else {
      ESP_LOGW(TAG,
               "!!! OTA 接口返回数据异常 (Len: "
               "%d)，可能当前版本无升级任务 !!!",
               content_length);
    }
  } else {
    ESP_LOGE(TAG, "!!! 请求 Studio OTA 接口失败，错误码: %d !!!", err);
  }

  esp_http_client_cleanup(client);
}

void WiFi_Cat1_OTADownload(uint16_t s, uint16_t e, uint8_t num) {
  ESP_LOGI(TAG, "WiFi_Cat1_OTADownload %d-%d", s, e);
}

void WiFi_Cat1_ActiveEvent(void) {
  // 各种定时事件
}
/*-------------------------------------------------*/
/*函数名：处理OTA服务器的数据                     */
/*参  数：data ：数据                              */
/*参  数：datalen ：数据长度                       */
/*参  数：page_index ：当前页索引                   */
/*参  数：is_last ：是否为最后一页                       */
/*参  数：ota_staflag ：OTA状态标志位, 0:网关, 1:子设备 */
/*返回值：无                                       */
/*-------------------------------------------------*/
void OTAServer_process(uint8_t *data, uint16_t datalen, uint32_t page_index,
                       uint8_t is_last, uint8_t ota_staflag) {
  // 处理 OTA 服务器的响应
  // 修正：分配内存时必须包含变长数组 data[] 的空间
  OTA_ZC_Chunk *new_chunk =
      (OTA_ZC_Chunk *)malloc(sizeof(OTA_ZC_Chunk) + datalen);
  if (new_chunk != NULL) {

    new_chunk->len = datalen;
    new_chunk->page_index = page_index;
    new_chunk->is_last = is_last;
    new_chunk->ota_staflag = ota_staflag;
    // 拷贝实际有效的数据到尾部的可变数组中
    memcpy(new_chunk->data, data, datalen);
    // 入队
    if (xQueueSend(OTA_ZC_Queue, new_chunk,
                   pdMS_TO_TICKS(OTA_ZC_SEND_TIMEOUT_MS)) != pdTRUE) {
      free(new_chunk);
      ESP_LOGI(TAG, "OTA_ZC_Queue send fail");
    }
  } else {
    //// 内存分配失败处理
    ESP_LOGI(TAG, "OTA_ZC_Queue send fail");
  }
}

/**
 * @brief 处理 MQTT 服务器下发的数据（透明传输 TCP 报文解析）
 *
 * @param data 串口收到的原始 MQTT 十六进制报文
 * @param data_len 报文总长度
 */
void MqttServer_ProcessData(uint8_t *data, uint16_t data_len) {
  if (data_len < 2)
    return;

  uint8_t packet_type = (data[0] & 0xF0) >> 4;

  switch (packet_type) {
  case 0x02: // CONNACK
    if (data[3] == 0x00) {
      ESP_LOGI(TAG, "MQTT 服务器登录成功!");
    } else {
      ESP_LOGE(TAG, "MQTT 服务器登录失败, 错误码: 0x%02X", data[3]);
    }
    break;

  case 0x03: // PUBLISH
  {
    uint32_t multiplier = 1;
    uint32_t remaining_length = 0;
    int index = 1;
    uint8_t encoded_byte;
    do {
      encoded_byte = data[index++];
      remaining_length += (encoded_byte & 127) * multiplier;
      multiplier *= 128;
    } while ((encoded_byte & 128) != 0 && index < data_len);

    uint16_t topic_len = (data[index] << 8) | data[index + 1];
    index += 2;

    char rx_topic[128] = {0};
    if (topic_len < sizeof(rx_topic)) {
      memcpy(rx_topic, &data[index], topic_len);
    }
    index += topic_len;

    uint16_t payload_len = remaining_length - topic_len - 2;
    char rx_payload[512] = {0};
    if (payload_len < sizeof(rx_payload)) {
      memcpy(rx_payload, &data[index], payload_len);
    }

    ESP_LOGI(TAG, "MQTT Topic: %s", rx_topic);
    ESP_LOGI(TAG, "MQTT Payload: %s", rx_payload);
    break;
  }

  case 0x0D: // PINGRESP
    ESP_LOGD(TAG, "收到 MQTT PINGRESP");
    break;

  default:
    ESP_LOGW(TAG, "收到其他 MQTT 报文类型: 0x%02X", packet_type);
    break;
  }
}

void start_Cat1Task(void *argument) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}