<<<<<<< HEAD
# Registro del componente principal.
# Incluimos solo los fuentes necesarios para la aplicación (excluyendo archivos de prueba).
# Agrega otros .c si deseas compilarlos.

idf_component_register(
    SRCS "main.c" "mfrc522_min.c"
    INCLUDE_DIRS "."
    REQUIRES driver spi_flash spiffs esp_timer esp_adc
=======
idf_component_register(
	SRCS "main.c" "mfrc522_min.c"
	INCLUDE_DIRS "."
	REQUIRES cjson esp_timer esp_wifi esp_event mqtt driver nvs_flash esp_netif vfs spiffs esp_adc esp_system
	PRIV_REQUIRES spi_flash
>>>>>>> new_timeline2
)
