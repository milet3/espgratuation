idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bsp_uart bsp_storage bsp_key bsp_led esp_mqtt)
