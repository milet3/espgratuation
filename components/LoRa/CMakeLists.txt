idf_component_register(SRCS "lora.c" "driver_llcc68.c" "driver_llcc68_interface.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_netif esp_event bsp_uart)

# 允许这个组件访问 main 文件夹下的头文件 (如 app_config.h)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/main")