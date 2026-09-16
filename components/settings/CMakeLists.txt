idf_component_register(
    SRCS "settings.c" "settings_nvs.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash
)