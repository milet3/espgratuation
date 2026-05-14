#ifndef __ESP_MQTT_H__
#define __ESP_MQTT_H__

#include "esp_err.h"
#include <stdint.h>

// 用于鉴权计算的 Token 结构体 (从原 STM32 代码移植)
typedef struct {
  char decodekey[128]; // 对设备密钥进行base64解码后的结果
  char StringForSignature
      [256]; // 进行StringForSignature字符串的建立，结果作为明文
  char signtemp[128]; // sign计算的临时缓冲区
  char sign[128];     // sign的最终结果
  char res[128];      // 存放res的字符串
  char signURL[128];  // sign进行URL编码后的结果
  char resURL[128];   // res进行URL编码后的结果
} Token_CB;

// 外部引用的 MQTT 变量
extern char TopicBuff[10][128]; // 二维数组，存放需要订阅的topic字符串
extern char TopicNum;           // 需要订阅的数据topic数量
extern char
    Mqtt_Password[512]; // 存放计算出来的 MQTT 鉴权密码 (扩大到 512 字节防截断)

/**
 * @brief URL 编码函数
 */
void URL_encode(char *data, int data_len, char *outdata);

/**
 * @brief 初始化 MQTT 参数 (生成鉴权密码, 组装 Topic)
 */
void MQTT_Init(void);

/**
 * @brief 启动 MQTT 客户端连接
 *
 * @param broker_uri MQTT 服务器 URI (例如 "mqtt://111.230.189.156:1883")
 * @return esp_err_t ESP_OK 成功, 其他失败
 */
esp_err_t esp_mqtt_app_start(const char *broker_uri);

/**
 * @brief 停止 MQTT 客户端
 */
void esp_mqtt_app_stop(void);

/**
 * @brief 发布 MQTT 消息
 *
 * @param topic 主题
 * @param data 数据
 * @param len 数据长度
 * @param qos QoS等级 (0, 1, 2)
 * @param retain 是否保留消息
 * @return int 成功返回 message_id，失败返回 -1
 */
int esp_mqtt_publish_msg(const char *topic, const char *data, int len, int qos,
                         int retain);

#endif // __ESP_MQTT_H__
