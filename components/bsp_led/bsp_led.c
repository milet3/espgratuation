#include "bsp_led.h"
#include "app_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BSP_LED";

void bsp_led_init(void) {
  // 配置 LED 引脚为推挽输出
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << LED_GW001_LED_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  // 默认关闭所有 LED (低电平点亮，则初始设为高)
  gpio_set_level(LED_GW001_LED_PIN, 1);

  ESP_LOGI(TAG, "LEDs initialized: GW_LED(IO%d)", LED_GW001_LED_PIN);
}