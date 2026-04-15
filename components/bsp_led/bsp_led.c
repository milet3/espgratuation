#include "bsp_led.h"
#include "esp_log.h"

static const char *TAG = "BSP_LED";

void bsp_led_init(void)
{
    // 在这里粘贴你的 STM32 LED 移植代码，并替换为 ESP-IDF 的 GPIO API
    // 例如：gpio_reset_pin(), gpio_set_direction() 等
    ESP_LOGI(TAG, "LED init porting...");
}