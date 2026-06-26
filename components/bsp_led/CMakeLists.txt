idf_component_register(SRCS "bsp_led.c" "led_strip_encoder.c"
                    INCLUDE_DIRS "include"
                    REQUIRES app_config esp_driver_rmt)
