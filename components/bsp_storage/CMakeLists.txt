idf_component_register(SRCS "bsp_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash freertos esp_system app_config)
