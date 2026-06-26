#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t bsp_led_init(void);
esp_err_t bsp_led_set(bool on);
esp_err_t bsp_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);

#endif // __BSP_LED_H__
