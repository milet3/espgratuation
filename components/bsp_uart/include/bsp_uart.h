#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#define UART_NUM_SOIL SOIL_UART_PORT
#define UART_NUM_CAT1 CAT1_PORT
#define UART_NUM_LORA LORA_UART_PORT

#define CAT1_APN "3gnet"
#define UART_BUF_SIZE (1024 * 4)
#define OTA_WRITE_MAX_RETRY (3)

esp_err_t Cat1_AT_Init(void);
esp_err_t Cat1_PPPoS_Init(void);
bool cat1_pppos_is_connected(void);
esp_netif_t *cat1_pppos_get_netif(void);
int bsp_uart_cat1_send(const char *data, int len);

#endif // __BSP_UART_H__
