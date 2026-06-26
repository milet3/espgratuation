#ifndef _MW1268_APP_H
#define _MW1268_APP_H

#include "esp_err.h"
#include <stdint.h>

/* -------------------------------------------------------
 *  状态定义
 * ------------------------------------------------------- */
typedef enum { LORA_RX_STA = 0, LORA_TX_STA, LORA_CFG_STA } _LORA_DEVICE_STA;

/* -------------------------------------------------------
 *  函数声明
 * ------------------------------------------------------- */

/**
 * @brief  LoRa 初始化 (配置为网关接收模式，使用出厂默认参数)
 */
esp_err_t LoRa_Init(void);

/**
 * @brief  LoRa 数据发送
 * @param  data 数据指针
 * @param  len  数据长度
 * @return 0 成功
 */
uint8_t LoRa_SendData(const uint8_t *data, uint16_t len);

/**
 * @brief  LoRa 主动事件处理 (接收子节点数据)
 */
void LoRa_ActiveEvent(void);

/**
 * @brief  向子节点发送在线查询指令
 */
void LoRa_QueryNodeOnline(void);

/**
 * @brief  控制子节点 LED
 */
void LoRa_ControlNodeLED(uint8_t on_off);

#endif
