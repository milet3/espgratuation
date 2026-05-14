#ifndef _MW1268_UART_H
#define _MW1268_UART_H

#include "driver/uart.h"
#include "esp_err.h"
#include <stdint.h>

/* LoRa 使用的串口号 (调试阶段搬到 UART2，避开 UART0 日志干扰) */
#ifndef LORA_UART_PORT
#define LORA_UART_PORT UART_NUM_2
#endif

/**
 * @brief  初始化 LoRa 模块所使用的串口
 * @param  baudrate  波特率
 * @retval ESP_OK 表示成功
 */
esp_err_t lora_uart_init(uint32_t baudrate);

/**
 * @brief  在接收缓冲区中查找指定字符串
 * @param  str  目标字符串
 * @retval 找到返回指针，未找到返回 NULL
 */
uint8_t *lora_check_cmd(uint8_t *str);

/**
 * @brief  向 MW1268 发送指令并等待应答
 *
 * @param  cmd       指令内容
 * @param  ack       期望的应答
 * @param  waittime  超时时间（单位：10 ms）
 * @retval 0 = 成功，1 = 超时/失败
 */
uint8_t lora_send_cmd(char *cmd, char *ack, uint16_t waittime);

/**
 * @brief  从串口读取原始数据
 * @param  buf      数据缓冲区
 * @param  len      期望读取长度
 * @param  timeout_ms  读取超时
 * @retval 实际读取长度
 */
int lora_uart_read_raw(uint8_t *buf, uint32_t len, uint32_t timeout_ms);

#endif
