idf_component_register(
	SRCS "main.c" "mfrc522_min.c"
	INCLUDE_DIRS "."
	REQUIRES cjson esp_timer esp_wifi esp_event mqtt driver nvs_flash esp_netif vfs spiffs esp_adc esp_system
	PRIV_REQUIRES spi_flash
)
