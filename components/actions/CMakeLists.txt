idf_component_register(
    SRCS "actions.c"
    INCLUDE_DIRS "."
    REQUIRES ble_spam fakeap settings script recon esp_wifi esp_timer)