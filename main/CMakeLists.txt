# Registro del componente principal.
# Incluimos solo los fuentes necesarios para la aplicación (excluyendo archivos de prueba).
# Agrega otros .c si deseas compilarlos.

idf_component_register(
    SRCS "main.c" "mfrc522_min.c"
    INCLUDE_DIRS "."
    REQUIRES driver spi_flash spiffs esp_timer esp_adc
)
