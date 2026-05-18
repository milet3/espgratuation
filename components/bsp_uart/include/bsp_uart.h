#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "driver/uart.h"
#include "esp_err.h"
#include <stdint.h>

// ==========================================
// 串口号定义 (从 app_config.h 中动态读取)
// ==========================================
#include "app_config.h"
#define UART_NUM_SOIL SOIL_UART_PORT
#define UART_NUM_CAT1 CAT1_PORT
#define UART_NUM_LORA LORA_UART_PORT
/*
#define UART_NUM_K210 UART_NUM_0
*/

// 默认 APN，通常物联网卡使用 internet，移动 cmnet，联通 3gnet
#define CAT1_APN "3gnet"

// 串口接收缓冲区大小
#define UART_BUF_SIZE (1024 * 4)

#define OTA_WRITE_MAX_RETRY (3) // OTA 写入最大重试次数

/**
 * @brief 初始化 Cat1 模块的 AT 架构 (串口透传/AT指令模式)
 *
 * @return esp_err_t
 */
esp_err_t Cat1_AT_Init(void);

/**
 * @brief 通过 Cat1/WiFi 串口发送数据
 *
 * @param data 数据指针
 * @param len 数据长度
 * @return int 实际发送的字节数
 */
int bsp_uart_cat1_send(const char *data, int len);

#endif // __BSP_UART_H__
