idf_component_register(SRCS "esp_mqtt.c" "utils_hmac.c"
                    INCLUDE_DIRS "include" "."
                    REQUIRES mqtt esp_netif)

# 允许这个组件访问 main 文件夹下的头文件 (如 app_config.h)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/main")