idf_component_register(
    SRCS "fakeap.c"
    INCLUDE_DIRS "."
    REQUIRES esp_netif esp_wifi esp_event nvs_flash ap_config ap_utils ssids
)