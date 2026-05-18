/**
 * @file    mw1268_usart3.h
 * @brief   MW1268 模块串口驱动（标准外设库版）
 *          USART3: PB10(TX) / PB11(RX)
 *          可选：USART1 printf 重定向（PA9-TX/PA10-RX）
 */

#ifndef __MW1268_USART3_H
#define __MW1268_USART3_H

#include "stm32f10x.h"
#include <stdarg.h>
#include <stdio.h>

#define USART3_MAX_RECV_LEN		400					//最大接收缓存字节数
#define USART3_MAX_SEND_LEN		400					//最大发送缓存字节数

extern uint8_t  USART3_RX_BUF[USART3_MAX_RECV_LEN];	//接收缓冲
extern uint8_t  USART3_TX_BUF[USART3_MAX_SEND_LEN];	//发送缓冲
extern volatile uint16_t USART3_RX_STA;   			//接收状态标记

/* -------------------------------------------------------
 *  printf 重定向选项
 * ------------------------------------------------------- */
#define MW1268_USE_USART1_PRINTF   0

void usart3_init(uint32_t bound);
void usart3_bpsset(uint8_t bps, uint8_t parity);
void u3_printf(char* fmt,...);
void usart3_rx(uint8_t enable);

#endif /* __MW1268_USART3_H */
