/*-------------------------------------------------*/
/*                                                 */
/*              操作WiFi功能的头文件               */
/*                                                 */
/*-------------------------------------------------*/

#ifndef __WIFI_CAT1_H
#define __WIFI_CAT1_H

#include "app_config.h"
#include "bsp_uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>
#include "ota_manager.h"

#define CAT1_TYPE 3

#define PACK_MAX_SIZE 1024

#define WIFI_RESET(x) gpio_set_level(WIFI_RESET_PIN, (x))

#define CAT1_POWER(x)                                                          \
  do {                                                                         \
    if (CAT1_POWER_STATE_PIN >= 0)                                             \
      gpio_set_level(CAT1_POWER_STATE_PIN, (x));                               \
  } while (0)
#define CAT1_POWER_STA                                                         \
  (CAT1_POWER_STA_PIN >= 0 ? gpio_get_level(CAT1_POWER_STA_PIN) : 0)
#define CAT1_NET_STA                                                           \
  (CAT1_NET_STA_PIN >= 0 ? gpio_get_level(CAT1_NET_STA_PIN) : 0)

#define PACK_NUM 6 // 鍙互璁板綍鐨勬渶澶ф暟鎹寘鐨勬暟閲?
typedef struct {
  uint16_t Data_Packsta; // 鍒嗗寘鐘舵€侊細0琛ㄧず鏁版嵁鏃犲垎鍖咃紝1琛ㄧず鏈夋暟鎹垎鍖?
  uint16_t Data_totle;             // 鐢ㄤ簬璁板綍宸茬粡澶勭悊鐨勬暟鎹殑闀垮害
  uint16_t Data_lenth[PACK_NUM];   // 鐢ㄤ簬璁板綍鏁村寘鏁版嵁鐨勯暱搴?
  uint16_t Data_num;               // 鐢ㄤ簬璁板綍鏈夊嚑涓暟鎹寘
  uint16_t Data_Packlen[PACK_NUM]; // 鐢ㄤ簬璁板綍鏁版嵁鍖呮暟鎹暱搴?
  char Data_Packbuff[PACK_NUM][PACK_MAX_SIZE]; // 鐢ㄤ簬璁板綍涓€涓暣鍖呯殑鏁版嵁缂撳啿鍖?
} Pack_CB;                                     // 鏁村寘鏁版嵁鎺у埗缁撴瀯浣?
#define PACK_CB_LEN sizeof(Pack_CB) // 鏁村寘鏁版嵁鎺у埗缁撴瀯浣撳崰鐢ㄧ殑瀛楄妭閲?

void WiFi_Cat1_InitGPIO(void); // 鍑芥暟璇存槑锛屽垵濮嬪寲WiFi 4G Cat1妯″潡 鎺у埗IO
void WiFi_Reset(void);         // 鍑芥暟璇存槑锛屽浣峎iFi妯″潡
void Cat1_Reset(void);         // 鍑芥暟璇存槑锛屽浣?G Cat1妯″潡
void WiFi_ProcessData(uint8_t *, uint16_t); // 鍑芥暟璇存槑锛屽鐞哤iFi妯″潡鐨勬暟鎹?
void Cat1_ProcessData(uint8_t *, uint16_t); // 鍑芥暟璇存槑锛屽鐞?G Cat1妯″潡鐨勬暟鎹?
void MqttServer_ProcessData(uint8_t *,
                            uint16_t); // 鍑芥暟璇存槑锛屽鐞哅QTT鏈嶅姟鍣ㄧ殑鏁版嵁
void WiFi_Cat1_ActiveEvent(void); // 鍑芥暟璇存槑锛學iFi妯″潡 or 4G Cat1妯″潡涓诲姩浜嬩欢
esp_err_t WiFi_Cat1_SubOnline(char sub_num, char mode); // 鍑芥暟璇存槑锛屽瓙璁惧涓婁笅绾?
void WiFi_Cat1_GatewayDataPost(float temp, float hum,
                               float lux); // 鍑芥暟璇存槑锛岀綉鍏虫暟鎹笂浼?
void WiFi_Cat1_NodeDataPost(float temp, float hum,
                            float lux); // 鍑芥暟璇存槑锛屽瓙鑺傜偣鏁版嵁涓婁紶
void WiFi_Cat1_SoilDataPost(float temp, float humi, float ec, float n, float p,
                            float k); // 鍑芥暟璇存槑锛屽湡澹や紶鎰熷櫒鍥哄畾涓婃姤
void WiFi_Cat1_AdcDataPost(float adc1, float adc2,
                           float adc3); // 鍑芥暟璇存槑锛孉DC鏁版嵁鍥哄畾涓婃姤
void WiFi_Cat1_AllDataPost(float air_temp, float air_hum, float air_lux,
                           float soil_temp, float soil_humi, float soil_ec,
                           float soil_n, float soil_p, float soil_k, float adc1,
                           float adc2,
                           float adc3); // 鍑芥暟璇存槑锛屾墍鏈変紶鎰熷櫒鏁版嵁鍚堝苟涓婃姤

/**
 * @brief 涓诲姩鑾峰彇瀛愯澶囧睘鎬?
 * @param sub_num 瀛愯澶囩储寮?(1:D001, 2:D002...)
 */
void WiFi_Cat1_SubPropertyGet(char sub_num);
void start_Cat1Task(void *argument); // 鍑芥暟璇存槑锛?G Cat1 鍚庡彴浠诲姟
void Cat1_AT_Mqtt_Task(
    void *pvParameters); // 鍑芥暟璇存槑锛孉T 妯″紡涓嬬殑 MQTT 鐩戞帶浠诲姟
esp_err_t
Cat1_AT_MqttPublish(const char *topic,
                    const char *payload); // 鍑芥暟璇存槑锛孉T 妯″紡涓嬬殑 MQTT 鍙戝竷

#endif