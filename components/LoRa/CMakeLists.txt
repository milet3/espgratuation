idf_component_register(SRCS "lora.c" "driver_llcc68.c" "driver_llcc68_interface.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_netif esp_event bsp_uart app_config)
