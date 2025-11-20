idf_component_register(
    SRCS "main.c" "mfrc522_min.c"
    REQUIRES driver esp_timer esp_adc
    PRIV_REQUIRES spi_flash log
    INCLUDE_DIRS ""
)
