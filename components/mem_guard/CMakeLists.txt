idf_component_register(
    SRCS "mem_guard.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_system esp_hw_support
)
