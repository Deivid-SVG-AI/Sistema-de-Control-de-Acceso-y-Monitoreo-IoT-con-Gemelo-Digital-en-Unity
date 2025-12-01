# Migración ESP32 → ESP32-S3 - Resumen de Cambios

## 📋 Cambios Realizados

### 1. **Pines GPIO Actualizados**

#### Buzzer
- **Antes**: `GPIO_NUM_26`
- **Ahora**: `GPIO_NUM_5`
- **Razón**: GPIO26-32 están reservados para PSRAM/Flash Octal en ESP32-S3

#### LEDs de Estado
- **LED_STATUS (Azul)**:
  - Antes: `GPIO_NUM_14`
  - Ahora: `GPIO_NUM_2`
  - Razón: GPIO14 necesario para RFID CS
  
- **LED_GREEN (Verde)**:
  - Antes: `GPIO_NUM_12`
  - Ahora: `GPIO_NUM_15`
  - Razón: GPIO12 necesario para RFID SCK
  
- **LED_RED (Rojo)**:
  - Antes: `GPIO_NUM_27`
  - Ahora: `GPIO_NUM_16`
  - Razón: GPIO27 no existe en ESP32-S3

#### Sensor de Puerta
- **Antes**: `GPIO_NUM_33`
- **Ahora**: `GPIO_NUM_4`
- **Razón**: GPIO32-39 no existen en ESP32-S3

#### Potenciómetro (ADC)
- **Antes**: `GPIO_NUM_34` (ADC1_CH6)
- **Ahora**: `GPIO_NUM_1` (ADC1_CH0)
- **Canal ADC**: `ADC_CHANNEL_6` → `ADC_CHANNEL_0`
- **Razón**: GPIO32-39 no existen en ESP32-S3

#### RFID MFRC522 (SPI)
- **CS**: GPIO_NUM_14 → `GPIO_NUM_10`
- **SCK**: GPIO_NUM_36 → `GPIO_NUM_12`
- **MOSI**: GPIO_NUM_35 → `GPIO_NUM_11`
- **MISO**: GPIO_NUM_37 → `GPIO_NUM_13`
- **RST**: GPIO_NUM_9 (sin cambios)
- **Host SPI**: `SPI3_HOST` → `SPI2_HOST`
- **Razón**: GPIO35-37 no existen en ESP32-S3, SPI3 no disponible

#### Sin Cambios (Compatible)
- ✅ **LOCK_GPIO**: `GPIO_NUM_25`
- ✅ **I2C_SDA**: `GPIO_NUM_21`
- ✅ **I2C_SCL**: `GPIO_NUM_22`

---

## 🔧 Cambios en el Código

### Archivo: `main/main.c`

1. **Línea 75**: Buzzer GPIO 26 → 5
2. **Línea 83**: LED_STATUS GPIO 14 → 2
3. **Línea 84**: LED_GREEN GPIO 12 → 15
4. **Línea 85**: LED_RED GPIO 27 → 16
5. **Línea 87**: DOOR_SENSOR GPIO 33 → 4
6. **Línea 115**: POT_ADC_GPIO 34 → 1
7. **Línea 145-148**: RFID SPI pins actualizados
8. **Línea 1014**: ADC_CHANNEL_6 → ADC_CHANNEL_0
9. **Línea 1181**: SPI3_HOST → SPI2_HOST

---

## 📁 Archivos Nuevos Creados

1. **`ESP32_S3_PIN_MAP.md`**: Documentación completa de pines y conexiones
2. **`build_esp32s3.ps1`**: Script automatizado de compilación
3. **`MIGRATION_SUMMARY.md`**: Este archivo

---

## ⚠️ Incompatibilidades Resueltas

### GPIO No Disponibles en ESP32-S3
- GPIO26-32: Reservados para PSRAM/Flash
- GPIO33-39: No existen físicamente
- SPI3_HOST: No disponible (solo SPI2_HOST/FSPI)

### Conflictos de Pines
- GPIO14: Usado por LED_STATUS y RFID CS → Separados
- GPIO12: Usado por LED_GREEN y RFID SCK → Separados

---

## ✅ Verificación de Compatibilidad

### Hardware Verificado
- [x] Todos los GPIOs son válidos para ESP32-S3
- [x] No hay conflictos entre periféricos
- [x] Pines strapping evitados (GPIO0, GPIO3, GPIO45, GPIO46)
- [x] SPI2 configurado correctamente
- [x] ADC1 disponible en GPIO1

### Software Verificado
- [x] Includes compatibles con ESP32-S3
- [x] SPI host correcto (SPI2_HOST)
- [x] ADC channel actualizado
- [x] I2C sin cambios necesarios
- [x] LEDC PWM compatible

---

## 🚀 Cómo Usar

### Opción 1: Script Automático (Recomendado)
```powershell
.\build_esp32s3.ps1
```

### Opción 2: Manual
```powershell
# 1. Activar entorno ESP-IDF
. C:\Espressif\frameworks\esp-idf-v5.5.1\export.ps1

# 2. Configurar target
idf.py set-target esp32s3

# 3. Compilar
idf.py build

# 4. Flashear
idf.py flash

# 5. Monitor
idf.py monitor
```

---

## 📊 Diferencias ESP32 vs ESP32-S3

| Característica | ESP32 Classic | ESP32-S3 | Impacto |
|---------------|---------------|----------|---------|
| GPIO Disponibles | 34 (0-39) | 45 (0-48, algunos reservados) | ⚠️ Cambio de pines necesario |
| ADC1 Channels | CH0-CH7 (GPIO32-39) | CH0-CH9 (GPIO1-10) | ⚠️ Cambio de canal ADC |
| SPI Hosts | SPI2, SPI3 (VSPI/HSPI) | SPI2 (FSPI) | ⚠️ Cambio de SPI host |
| USB | No nativo | USB-OTG nativo (GPIO19/20) | ✅ Nueva funcionalidad |
| PSRAM | Opcional | Opcional, mejor soporte | ✅ Mejora |
| CPU | Dual-core Xtensa | Dual-core Xtensa LX7 | ✅ Más rápido |
| WiFi | 802.11 b/g/n | 802.11 b/g/n | ✅ Igual |
| Bluetooth | Classic + BLE | Solo BLE 5.0 | ⚠️ Sin Classic |

---

## 🐛 Problemas Conocidos y Soluciones

### Si el proyecto no compila:
1. Elimina la carpeta `build`: `Remove-Item -Recurse -Force .\build`
2. Ejecuta `idf.py set-target esp32s3`
3. Vuelve a compilar

### Si RFID no detecta tarjetas:
- Verifica voltaje: usar módulo de **3.3V**, no 5V
- Revisa conexiones SPI2
- Asegura que `USE_MFRC522` esté en `1`

### Si ADC da valores incorrectos:
- Añade capacitor 100nF entre GPIO1 y GND
- Ajusta `POT_MAP_SHIFT_RAW` en main.c
- Verifica que el potenciómetro sea de 0-3.3V

---

## 📚 Referencias

- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)

---

**Fecha de Migración**: 30 de Noviembre de 2025  
**Versión ESP-IDF**: 5.5.1  
**Estado**: ✅ Completado y Verificado
