idf_component_register(SRCS "esp_mqtt.c" "utils_hmac.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES mqtt esp_netif app_config)
