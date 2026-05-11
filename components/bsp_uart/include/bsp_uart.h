#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "esp_err.h"
#include <stdint.h>

// ==========================================
// 串口号定义 (ESP32S3 有 3 个硬件 UART: UART0, UART1, UART2)
// 因为 ESP32-S3 自带了 USB-Serial/JTAG 控制器用于 Log 和下载，
// 所以 UART0、UART1、UART2 都可以自由分配给外设。
// 目前分配:
// UART0: 分配给 K210 模块 (GPIO 9, 10)
// UART1: 分配给土壤传感器 (GPIO 32, 33)
// UART2: 分配给 Cat1 模块 (GPIO 4, 5)
// ==========================================
#define UART_NUM_SOIL UART_NUM_1
#define UART_NUM_CAT1 UART_NUM_2
#define UART_NUM_K210 UART_NUM_0
#include "app_config.h"
// 默认 APN，通常物联网卡使用 internet，移动 cmnet，联通 3gnet
#define CAT1_APN "3gnet"

// 串口接收缓冲区大小
#define UART_BUF_SIZE (1024 * 4)

#define OTA_WRITE_MAX_RETRY (3) // OTA 写入最大重试次数

/**
 * @brief 初始化 Cat1 模块的 PPPoS 拨号
 *
 * @return esp_err_t
 */
esp_err_t Cat1_PPPoS_Init(void);

/**
 * @brief 通过 Cat1/WiFi 串口发送数据
 *
 * @param data 数据指针
 * @param len 数据长度
 * @return int 实际发送的字节数
 */
int bsp_uart_cat1_send(const char *data, int len);

#endif // __BSP_UART_H__
