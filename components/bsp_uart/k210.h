#ifndef __K210_H__
#define __K210_H__

#include "esp_err.h"

/**
 * @brief 初始化 K210 模块的串口
 *
 * @return esp_err_t ESP_OK 表示成功，其他表示失败
 */
esp_err_t k210_uart_init(void);

/**
 * @brief 发送数据到 K210
 *
 * @param data 数据指针
 * @param len 数据长度
 * @return int 实际发送的字节数
 */
int k210_uart_send(const uint8_t *data, int len);

/**
 * @brief 从 K210 读取数据
 * 
 * @param buf 接收缓冲区指针
 * @param len 期望读取的长度
 * @param timeout_ms 超时时间（毫秒）
 * @return int 实际读取的字节数
 */
int k210_uart_read(uint8_t *buf, int len, uint32_t timeout_ms);

/**
 * @brief 上报害虫识别结果到云端
 * 
 * @param status 0: 无害虫, 1: 发现害虫
 */
void k210_report_pest_status(uint8_t status);

#endif // __K210_H__
