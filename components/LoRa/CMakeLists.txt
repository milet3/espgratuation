idf_component_register(SRCS "mw1268_app.c" "mw1268_uart.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver app_config bsp_uart)
