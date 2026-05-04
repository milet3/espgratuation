idf_component_register(SRCS "esp_mqtt.c" "utils_hmac.c" "utils_sha1.c" "utils_md5.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES mqtt esp_netif app_config json bsp_uart)
