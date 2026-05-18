idf_component_register(SRCS "bsp_uart.c" "wifi_cat1.c" "soil_sensor.c" "wifi_manager.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES driver esp_ringbuf esp_netif esp_event nvs_flash esp_mqtt espressif__esp_modem LoRa app_config bsp_storage esp_http_client esp_wifi esp_http_server lwip)
