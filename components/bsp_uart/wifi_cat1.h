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

#define OTA_ZC_QUEUE_LEN 8 // 零拷贝指针队列深度（仅存指针），控制可缓冲的分片数
#define OTA_ZC_SEND_TIMEOUT_MS 200 // 生产者入队超时时间（毫秒），用于背压等待
#define OTA_ZC_SEND_RETRY 3 // 入队失败后的重试次数（超时重试）
#define OTA_RANGE_SIZE 256  // 单次分片/页大小（字节）
#define OTA_ZC_WRITE_RETRY_MAX 2 // Writer 写失败时的额外重试次数
#define OTA_ZC_VERIFY_WRITE 1 // 写后校验开关：1启用回读校验，0关闭
#define CAT1_TYPE 3 // 1:Air780模块  2：Air724模块   3：EC800M模块

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

#define PACK_NUM 6 // 可以记录的最大数据包的数量
typedef struct {
  uint16_t Data_Packsta; // 分包状态：0表示数据无分包，1表示有数据分包
  uint16_t Data_totle;             // 用于记录已经处理的数据的长度
  uint16_t Data_lenth[PACK_NUM];   // 用于记录整包数据的长度
  uint16_t Data_num;               // 用于记录有几个数据包
  uint16_t Data_Packlen[PACK_NUM]; // 用于记录数据包数据长度
  char Data_Packbuff[PACK_NUM][PACK_MAX_SIZE]; // 用于记录一个整包的数据缓冲区
} Pack_CB;                                     // 整包数据控制结构体
#define PACK_CB_LEN sizeof(Pack_CB) // 整包数据控制结构体占用的字节量

typedef struct {
  int tid;              // OTA升级tid
  int ota_num;          // 总的下载次数
  int ota_counter;      // 当前第几次下载
  int ota_last;         // 最后一个数据包下载量
  uint16_t ota_staflag; // 0:网关ota 1:子设备ota
} OTA_CB;               // OTA控制结构体

void WiFi_Cat1_InitGPIO(void); // 函数说明，初始化WiFi 4G Cat1模块 控制IO
void WiFi_Reset(void);         // 函数说明，复位WiFi模块
void Cat1_Reset(void);         // 函数说明，复位4G Cat1模块
void WiFi_ProcessData(uint8_t *, uint16_t); // 函数说明，处理WiFi模块的数据
void Cat1_ProcessData(uint8_t *, uint16_t); // 函数说明，处理4G Cat1模块的数据
void MqttServer_ProcessData(uint8_t *,
                            uint16_t); // 函数说明，处理MQTT服务器的数据
void OTAServer_process(uint8_t *data, uint16_t datalen, uint32_t page_index,
                       uint8_t ota_staflag,
                       uint8_t is_last); // 函数说明，处理OTA服务器的数据
void WiFi_Cat1_ActiveEvent(void); // 函数说明，WiFi模块 or 4G Cat1模块主动事件
void WiFi_Cat1_SubOnline(char, char); // 函数说明，子设备上下线
void WiFi_Cat1_GatewayDataPost(float temp, float hum,
                               float lux); // 函数说明，网关数据上传
void WiFi_Cat1_NodeDataPost(float temp, float hum,
                            float lux); // 函数说明，子节点数据上传
void WiFi_Cat1_SoilDataPost(float temp, float humi, float ec, float n, float p,
                            float k); // 函数说明，土壤传感器固定上报
void WiFi_Cat1_AdcDataPost(float adc1, float adc2,
                           float adc3); // 函数说明，ADC数据固定上报
void WiFi_Cat1_AllDataPost(float air_temp, float air_hum, float air_lux,
                           float soil_temp, float soil_humi, float soil_ec,
                           float soil_n, float soil_p, float soil_k, float adc1,
                           float adc2,
                           float adc3); // 函数说明，所有传感器数据合并上报

/**
 * @brief 主动获取子设备属性
 * @param sub_num 子设备索引 (1:D001, 2:D002...)
 */
void WiFi_Cat1_SubPropertyGet(char sub_num);
void WiFi_Cat1_ReportVersion(const char *id); // 函数说明，上报当前版本号
const char *WiFi_Cat1_GetRuntimeFirmwareVersion(void);
void WiFi_Cat1_StartOTA(const char *url, const char *token,
                        uint8_t ota_staflag); // 函数说明，开始OTA下载
void Studio_OTA_CheckTask(void); // 函数说明，新版 Studio OTA 检查
void WiFi_Cat1_PropertyVersion(uint8_t); // 函数说明，向服务器上传版本号
void WiFi_Cat1_CheckOTATask(uint8_t); // 函数说明，查询是否有OTA任务
void WiFi_Cat1_OTADownload(uint16_t, uint16_t,
                           uint8_t); // 函数说明，OTA下载新版本程序数据
void start_Cat1Task(void *argument); // 函数说明，4G Cat1 后台任务
void Cat1_AT_Mqtt_Task(
    void *pvParameters); // 函数说明，AT 模式下的 MQTT 监控任务
esp_err_t
Cat1_AT_MqttPublish(const char *topic,
                    const char *payload); // 函数说明，AT 模式下的 MQTT 发布

typedef struct {
  uint16_t len; // 包体长度（字节数），仅指 data[] 实际有效数据长度
  uint32_t page_index; // 写入FLASH的页索引（按目标分区+当前分片计算）
  uint8_t ota_staflag; // 0：网关OTA；1：子设备OTA，用于区分写入区域
  uint8_t is_last; // 是否为最后一个分片：1表示最后一片（写完做收尾）
  uint8_t data[]; // 变长数组，紧随结构体的有效数据载荷（零拷贝传递）
} OTA_ZC_Chunk;

typedef struct {
  uint32_t enqueued; // 成功入队的分片数量（生产端实际投递成功次数）
  uint32_t
      enqueue_blocked; // 入队被阻塞/等待超时重试的次数（包含成功前的等待计数）
  uint32_t enqueue_fail; // 入队最终失败次数（超过重试上限或超时未入队）
  uint32_t processed;   // Writer 实际处理（写入完成）的分片数量
  uint32_t write_retry; // 写页后校验失败导致的写重试次数累计
  uint32_t write_fail;  // 写页最终失败次数（重试仍失败）
} OTA_ZC_Stats;

extern QueueHandle_t OTA_ZC_Queue; // 零拷贝分片指针队列（仅传指针）
extern OTA_ZC_Stats g_ota_zc_stats; // 运行期统计信息（用于压测与调优）
#endif
