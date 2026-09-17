idf_component_register(
    SRCS "web.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server spiffs settings actions recon harvest script power ble_spam)