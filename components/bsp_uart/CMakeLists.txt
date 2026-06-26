idf_component_register(SRCS "ota_manager.c" "bsp_uart.c" "wifi_cat1.c" "soil_sensor.c" "wifi_manager.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES mem_guard driver esp_ringbuf esp_netif esp_event nvs_flash esp_mqtt espressif__esp_modem LoRa app_config bsp_storage esp_http_client esp_https_ota esp_wifi esp_http_server lwip app_update)

# wifi_cat1.c 中 snprintf 的 format-truncation 警告是编译器静态分析误报
# (OneNET API 返回的 URL/设备名实际远小于缓冲区)，抑制该文件此项警告
set_source_files_properties(wifi_cat1.c PROPERTIES COMPILE_FLAGS -Wno-format-truncation)
