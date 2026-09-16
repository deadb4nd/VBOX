idf_component_register(
    SRCS "recon.c"
    INCLUDE_DIRS "."
    REQUIRES recon_core harvest esp_wifi esp_timer freertos log)