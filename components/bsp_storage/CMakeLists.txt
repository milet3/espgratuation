idf_component_register(SRCS "bsp_storage.c"
                    INCLUDE_DIRS "include"
                    REQUIRES nvs_flash)

# 允许这个组件访问 main 文件夹下的头文件 (如 app_config.h)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/main")