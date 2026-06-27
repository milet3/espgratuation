idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES mem_guard crash_report LVGL bsp_uart bsp_storage bsp_led esp_mqtt IIC_SENSOR app_config esp_http_client json app_update)
