idf_component_register(
    SRCS "actions.c"
    INCLUDE_DIRS "."
    REQUIRES ble_spam fakeap settings esp_wifi
)