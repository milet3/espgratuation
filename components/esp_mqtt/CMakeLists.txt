idf_component_register(SRCS "esp_mqtt.c" "utils_hmac.c" "utils_sha1.c" "utils_md5.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES mem_guard mqtt esp_netif app_config json bsp_uart LoRa bsp_led)
