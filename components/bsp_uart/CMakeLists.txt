idf_component_register(SRCS "bsp_uart.c" "wifi_manager.c" "wifi_cat1.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES driver esp_ringbuf esp_netif esp_wifi esp_event nvs_flash esp_http_server esp_mqtt espressif__esp_modem LoRa app_config)
