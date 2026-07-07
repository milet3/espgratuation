#include "wifi_cat1.h"
#include "ota_manager.h"
#include "app_config.h"
#include "bsp_uart.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mqtt.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "stdio.h"
#include "string.h"

extern char Mqtt_Password[];

__attribute__((unused)) static char g_at_rx_buffer[1024] = {0};
static volatile bool g_at_data_ready = false;
__attribute__((unused)) static SemaphoreHandle_t g_at_rx_mutex = NULL;

static esp_err_t Cat1_Send_AT_Command(const char *cmd, uint32_t timeout_ms,
                                      const char *expected_resp);

Pack_CB pack;
static const char *TAG = "WIFI_CAT1";

esp_err_t WiFi_Cat1_SubOnline(char sub_num, char mode)
{
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return ESP_ERR_NO_MEM;

  // 使用简单的 13 位以太网 ID
  cJSON_AddStringToObject(root, "id", "2506");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 关键修改：直接添加到 params 下，不要建数组！
  cJSON_AddStringToObject(params, "productID", SUB_PRODUCTID);
  cJSON_AddStringToObject(params, "deviceName", DeviceNameBuff[(int)sub_num]);

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data)
  {
    char temptopic[128];
    if (mode == 0)
    {
      snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/logout",
               GW_PRODUCTID, GW_DEVICENAME);
    }
    else
    {
      snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/login",
               GW_PRODUCTID, GW_DEVICENAME);
    }

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "SubOnline sent (Direct Params Format): %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
  return ESP_OK;
}

/**
 * @brief 网关自身数据上报 (使用普通的 property/post)
 */
void WiFi_Cat1_GatewayDataPost(float temp, float hum, float lux)
{
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以太网 ID
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
  cJSON *lux_obj = cJSON_AddObjectToObject(params, ATTRIBUTE7);
  cJSON_AddItemToObject(lux_obj, "value", cJSON_CreateRaw(l_str));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data)
  {
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
 * @brief 子设备代理上报(按照新版 OneNET Studio pack/post 规范重构)
 * 关键点：
 * 1. 使用 pack/post 主题
 * 2. params 为数组，每个元素包含 identity (PID/SN) 和 properties
 * 3. properties 内每个属性必须嵌套 {"value": xxx}
 */
void WiFi_Cat1_NodeDataPost(float temp, float hum, float lux)
{
  // 关键保护逻辑：仅当 MQTT 已连接 且 LoRa 已确认通信 时
  // 子设备尚未报备上线时，才执行上线报备
  if ((SysCB.SysEventFlag & CONNECT_MQTT) &&
      (SysCB.SysEventFlag & SUB_LORA_CONFIRMED) &&
      !(SysCB.SysEventFlag & SUB_ONLINE_READY))
  {
    ESP_LOGW(TAG, "LoRa 通信已确认，正在向 OneNET 报备子设备上线...");
    WiFi_Cat1_SubOnline(1, 1);
    SysCB.SysEventFlag |= SUB_ONLINE_READY;
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 1. 基础字段
  cJSON_AddStringToObject(root, "id", "123456");
  cJSON_AddStringToObject(root, "version", "1.0");

  // 2. params 为数组(pack 接口核心)
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
  if (post_data)
  {
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
                            float k)
{
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以太网 ID
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

  // 氮(double)
  cJSON *n_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_N);
  cJSON_AddItemToObject(n_obj, "value", cJSON_CreateRaw(sn_str));

  // 氮(double)
  cJSON *p_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_P);
  cJSON_AddItemToObject(p_obj, "value", cJSON_CreateRaw(sp_str));

  // 氮(double)
  cJSON *k_obj = cJSON_AddObjectToObject(params, ATTRIBUTE_SOIL_K);
  cJSON_AddItemToObject(k_obj, "value", cJSON_CreateRaw(sk_str));

  char *post_data = cJSON_PrintUnformatted(root);
  if (post_data)
  {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/property/post",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "SoilDataPost sent to %s: %s", temptopic, post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_AdcDataPost(float adc1, float adc2, float adc3)
{
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以太网 ID
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
  if (post_data)
  {
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
                           float adc2, float adc3)
{
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以太网 ID
  cJSON_AddStringToObject(root, "id", "5011");
  cJSON_AddStringToObject(root, "version", "1.0");
  cJSON *params = cJSON_AddObjectToObject(root, "params");

  // 精度修复：使用 cJSON_CreateRaw 强制转换两位小数
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
  if (post_data)
  {
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
 * @brief 主动获取子设备属性(按照 OneNET 规范)
 * @param sub_num 子设备索引(对应 DeviceNameBuff)
 */
void WiFi_Cat1_SubPropertyGet(char sub_num)
{
  cJSON *root = cJSON_CreateObject();
  if (root == NULL)
    return;

  // 使用简单的 13 位以太网 ID
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
  if (post_data)
  {
    char temptopic[128];
    snprintf(temptopic, sizeof(temptopic), "$sys/%s/%s/thing/sub/property/get",
             GW_PRODUCTID, GW_DEVICENAME);

    Cat1_AT_MqttPublish(temptopic, post_data);
    ESP_LOGI(TAG, "SubPropertyGet sent: %s", post_data);
    free(post_data);
  }

  cJSON_Delete(root);
}

void WiFi_Cat1_InitGPIO(void)
{
  if (CAT1_POWER_STATE_PIN >= 0)
  {
    gpio_set_direction(CAT1_POWER_STATE_PIN, GPIO_MODE_OUTPUT);
  }
  if (CAT1_POWER_STA_PIN >= 0)
  {
    gpio_set_direction(CAT1_POWER_STA_PIN, GPIO_MODE_INPUT);
  }
  if (CAT1_NET_STA_PIN >= 0)
  {
    gpio_set_direction(CAT1_NET_STA_PIN, GPIO_MODE_INPUT);
  }
}

void Cat1_Reset(void)
{
  if (CAT1_POWER_STA == 1)
  { // 如果目前处于关机状态，进入该分支
    ESP_LOGI(
        TAG,
        "\r\n目前4G Cat1模块处于关机状态，准备开机\r\n"); // 串口输出信息 //
                                                          // 串口输出信息
    CAT1_POWER(1);                                        // 拉高
    vTaskDelay(pdMS_TO_TICKS(1500));                      // 延时
    CAT1_POWER(0);                                        // 开机成功了，拉低
  }
  else
  { // 反之表示目前处于开机状态，进入该分支
    ESP_LOGI(TAG,
             "\r\n目前4G Cat1模块处于开机状态，准备重启\r\n"); // 串口输出信息
    CAT1_POWER(1);                                             // 先拉高
    vTaskDelay(pdMS_TO_TICKS(1500));                           // 延时
    CAT1_POWER(0);                                             // 关机成功了，拉低
    ESP_LOGI(TAG, "\r\n关机成功，准备开机\r\n");               // 串口输出信息
    vTaskDelay(pdMS_TO_TICKS(6000));                           // 延时
    CAT1_POWER(1);                                             // 拉高
    vTaskDelay(pdMS_TO_TICKS(1500));                           // 延时
    CAT1_POWER(0);                                             // 开机成功了，拉低
  }
  ESP_LOGI(TAG,
           "开机成功，请等待4G Cat1模块注册上网......\r\n"); // 串口输出信息
}

static esp_err_t Cat1_Send_AT_Command(const char *cmd, uint32_t timeout_ms,
                                      const char *expected_resp)
{
  bsp_uart_cat1_send(cmd, strlen(cmd));
  uint32_t start_time = xTaskGetTickCount();
  if (g_at_rx_mutex)
  {
    xSemaphoreTake(g_at_rx_mutex, portMAX_DELAY);
    memset(g_at_rx_buffer, 0, sizeof(g_at_rx_buffer));
    g_at_data_ready = false;
    xSemaphoreGive(g_at_rx_mutex);
  }
  while ((xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS < timeout_ms)
  {
    if (g_at_data_ready)
    {
      xSemaphoreTake(g_at_rx_mutex, portMAX_DELAY);
      if (strstr(g_at_rx_buffer, "RDY"))
      {
        xSemaphoreGive(g_at_rx_mutex);
        return ESP_FAIL;
      }
      if (expected_resp && strstr(g_at_rx_buffer, expected_resp))
      {
        xSemaphoreGive(g_at_rx_mutex);
        return ESP_OK;
      }
      xSemaphoreGive(g_at_rx_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return ESP_ERR_TIMEOUT;
}

void Cat1_AT_Mqtt_Task(void *pvParameters)
{
  static char at_cmd[1024];
  ESP_LOGI(TAG, "Cat1 MQTT 监控任务已启动 (带启动保护延时)...");
  vTaskDelay(pdMS_TO_TICKS(10000));
  for (;;)
  {
    if (SysCB.SysEventFlag & CONNECT_WIFI)
    {
      vTaskDelay(pdMS_TO_TICKS(30000));
      continue;
    }
    if (SysCB.SysEventFlag & CONNECT_MQTT)
    {
      if (Cat1_Send_AT_Command("AT\r\n", 1000, "OK") != ESP_OK)
      {
        SysCB.SysEventFlag &= ~CONNECT_MQTT;
      }
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }
    int retry_main = 0;
    const int max_retry_main = 5;
    while (retry_main < max_retry_main)
    {
      Cat1_Send_AT_Command("AT+QIDEACT=1\r\n", 3000, "OK");
      vTaskDelay(pdMS_TO_TICKS(2000));
      if (Cat1_Send_AT_Command("AT+CPIN?\r\n", 5000, "+CPIN: READY") != ESP_OK)
        goto retry_init;
      vTaskDelay(pdMS_TO_TICKS(1000));
      bool registered = false;
      for (int i = 0; i < 15; i++)
      {
        if (Cat1_Send_AT_Command("AT+CGREG?\r\n", 2000, "+CGREG: 0,1") ==
                ESP_OK ||
            Cat1_Send_AT_Command("AT+CGREG?\r\n", 2000, "+CGREG: 0,5") ==
                ESP_OK)
        {
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
      if (Cat1_Send_AT_Command(at_cmd, 10000, "+QMTCONN: 0,0,0") == ESP_OK)
      {
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

esp_err_t Cat1_AT_MqttPublish(const char *topic, const char *payload)
{
  if (topic == NULL || payload == NULL)
    return ESP_ERR_INVALID_ARG;
  if (SysCB.SysEventFlag & CONNECT_WIFI)
  {
    int msg_id = esp_mqtt_publish_msg(topic, payload, strlen(payload), 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
  }
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+QMTPUB=0,0,0,0,\"%s\"\r\n", topic);
  if (Cat1_Send_AT_Command(cmd, 2000, ">") != ESP_OK)
  {
    SysCB.SysEventFlag &= ~CONNECT_MQTT;
    return ESP_FAIL;
  }
  int total_len = strlen(payload);
  int packet_size = 200;
  int sent_len = 0;
  while (sent_len < total_len)
  {
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

void start_Cat1Task(void *argument)
{
  uint8_t *data = (uint8_t *)malloc(256);
  if (g_at_rx_mutex == NULL)
    g_at_rx_mutex = xSemaphoreCreateMutex();
  for (;;)
  {
    if (SysCB.SysEventFlag & CONNECT_WIFI)
    {
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    int len = uart_read_bytes(UART_NUM_CAT1, data, 255, pdMS_TO_TICKS(100));
    if (len > 0)
    {
      data[len] = '\0';
      if (g_at_rx_mutex)
      {
        xSemaphoreTake(g_at_rx_mutex, portMAX_DELAY);
        strncat(g_at_rx_buffer, (char *)data,
                sizeof(g_at_rx_buffer) - strlen(g_at_rx_buffer) - 1);
        g_at_data_ready = true;
        xSemaphoreGive(g_at_rx_mutex);
      }
      if (strstr((char *)data, "RDY"))
      {
        SysCB.SysEventFlag &= ~CONNECT_MQTT;
      }
      else if (strstr((char *)data, "+QMTCONN: 0,0,0"))
      {
        SysCB.SysEventFlag |= CONNECT_MQTT;
      }
      else if (strstr((char *)data, "+QMTSTAT: 0,"))
      {
        SysCB.SysEventFlag &= ~CONNECT_MQTT;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  free(data);
  vTaskDelete(NULL);
}
