idf_component_register(
    SRCS "ble_beacon.c" "gap.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nimble nvs_flash esp_hw_support
)
