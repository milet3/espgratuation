#include "wifi_cat1.h"
#include "app_config.h"
#include "bsp_uart.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mqtt.h"
#include "esp_wifi.h" // 补充缺失的头文件
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
  char temptopic[128];
  char tempdata[512];

  // OneNet 物模型属性上报主题
  snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
           GW_PRODUCTID, GW_DEVICENAME);

  // 构造 JSON 数据包
  // 使用 app_config.h 中定义的 ATTRIBUTE5 (temperature), ATTRIBUTE6 (humidity),
  // ATTRIBUTE7 (lightlux)
  snprintf(tempdata, sizeof(tempdata),
           "{\"id\":\"123\",\"version\":\"1.0\",\"params\":{"
           "\"%s\":{\"value\":%.2f},"
           "\"%s\":{\"value\":%.2f},"
           "\"%s\":{\"value\":%.2f}"
           "}}",
           ATTRIBUTE5, temp, ATTRIBUTE6, hum, ATTRIBUTE7, lux);

  esp_mqtt_publish_msg(temptopic, tempdata, strlen(tempdata), 0, 0);
  ESP_LOGI(TAG, "GatewayDataPost sent to %s: %s", temptopic, tempdata);
}

/*-------------------------------------------------*/
/*函数名：复位4G Cat1模块                          */
/*参  数：无                                       */
/*返回值：无                                       */
/*-------------------------------------------------*/
void Cat1_InitGPIO(void) {

  gpio_set_direction(CAT1_POWER_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(CAT1_POWER_STA_PIN, GPIO_MODE_INPUT);
  gpio_set_direction(CAT1_NET_STA_PIN, GPIO_MODE_INPUT);
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

void WiFi_Cat1_PropertyVersion(uint8_t num) {
  ESP_LOGI(TAG, "WiFi_Cat1_PropertyVersion %d", num);
}

void WiFi_Cat1_CheckOTATask(uint8_t num) {
  ESP_LOGI(TAG, "WiFi_Cat1_CheckOTATask %d", num);
}

typedef struct {
  char url[256];
  uint8_t ota_staflag;
} ota_task_args_t;

static void ota_download_task(void *pvParameters) {
  ota_task_args_t *args = (ota_task_args_t *)pvParameters;
  ESP_LOGI(TAG, "开始从 %s 下载 OTA 固件", args->url);

  esp_http_client_config_t config = {
      .url = args->url,
      .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "HTTP 客户端初始化失败");
    free(args);
    vTaskDelete(NULL);
    return;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "无法打开 HTTP 连接: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    free(args);
    vTaskDelete(NULL);
    return;
  }

  int content_length = esp_http_client_fetch_headers(client);
  if (content_length <= 0) {
    ESP_LOGE(TAG, "获取固件大小失败");
    esp_http_client_cleanup(client);
    free(args);
    vTaskDelete(NULL);
    return;
  }

  uint8_t *buffer = malloc(OTA_RANGE_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "内存分配失败");
    esp_http_client_cleanup(client);
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

  free(buffer);
  esp_http_client_cleanup(client);
  free(args);
  vTaskDelete(NULL);
}

void WiFi_Cat1_StartOTA(const char *url, uint8_t ota_staflag) {
  ota_task_args_t *args = malloc(sizeof(ota_task_args_t));
  if (args == NULL)
    return;

  strncpy(args->url, url, sizeof(args->url) - 1);
  args->url[sizeof(args->url) - 1] = '\0';
  args->ota_staflag = ota_staflag;

  xTaskCreate(ota_download_task, "ota_download", 8192, args, 5, NULL);
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
  OTA_ZC_Chunk *new_chunk = (OTA_ZC_Chunk *)malloc(sizeof(OTA_ZC_Chunk));
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
  }
}