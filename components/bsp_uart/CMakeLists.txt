idf_component_register(SRCS "bsp_uart.c" "wifi_cat1.c" "k210.c" "soil_sensor.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES driver esp_ringbuf esp_netif esp_event nvs_flash esp_mqtt espressif__esp_modem LoRa app_config esp_http_client)
