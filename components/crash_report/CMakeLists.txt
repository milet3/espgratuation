idf_component_register(
    SRCS "crash_report.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_system esp_timer espcoredump
)
