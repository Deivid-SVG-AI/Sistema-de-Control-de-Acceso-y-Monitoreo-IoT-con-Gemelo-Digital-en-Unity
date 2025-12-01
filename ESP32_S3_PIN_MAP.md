# Mapeo de Pines ESP32-S3 - Sistema de Acceso

## ⚠️ Cambios Importantes desde ESP32 Classic

Este proyecto ha sido migrado de **ESP32 Classic** a **ESP32-S3**. Todos los pines han sido actualizados para compatibilidad.

---

## 📌 Tabla de Pines Actualizada

| Función | Pin ESP32-S3 | Pin Original ESP32 | Notas |
|---------|--------------|-------------------|-------|
| **BUZZER** | GPIO5 | GPIO26 | GPIO26-32 usado por PSRAM en S3 |
| **LED_STATUS** (Azul) | GPIO2 | GPIO14 | GPIO14 en conflicto con RFID CS |
| **LED_GREEN** (Verde) | GPIO15 | GPIO12 | GPIO12 usado por RFID SCK |
| **LED_RED** (Rojo) | GPIO16 | GPIO27 | GPIO27 no existe en S3 |
| **DOOR_SENSOR** (Reed) | GPIO4 | GPIO33 | GPIO33 no existe en S3 |
| **LOCK** (Relay/Electroimán) | GPIO25 | GPIO25 | ✅ Sin cambios |
| **POTENTIOMETER** (ADC) | GPIO1 (ADC1_CH0) | GPIO34 (ADC1_CH6) | GPIO32-39 no existen en S3 |
| **I2C_SDA** (LCD) | GPIO21 | GPIO21 | ✅ Sin cambios |
| **I2C_SCL** (LCD) | GPIO22 | GPIO22 | ✅ Sin cambios |
| **RFID_CS** | GPIO10 | GPIO14 | Resuelve conflicto con LED |
| **RFID_SCK** | GPIO12 | GPIO36 | GPIO36 no existe en S3 |
| **RFID_MOSI** | GPIO11 | GPIO35 | GPIO35 no existe en S3 |
| **RFID_MISO** | GPIO13 | GPIO37 | GPIO37 no existe en S3 |
| **RFID_RST** | GPIO9 | GPIO9 | ✅ Sin cambios |

---

## 🔌 Conexiones de Hardware

### LEDs
- **GPIO2** (LED_STATUS - Azul): Resistencia 220Ω → LED → GND
- **GPIO15** (LED_GREEN - Verde): Resistencia 220Ω → LED → GND
- **GPIO16** (LED_RED - Rojo): Resistencia 220Ω → LED → GND

### Buzzer (PWM)
- **GPIO5**: Buzzer activo 5V (+ a GPIO5, - a GND)

### Sensor de Puerta (Reed Switch)
- **GPIO4**: Pull-up interno habilitado
  - Un terminal del reed a GPIO4
  - Otro terminal a GND
  - Cerrado = GPIO LOW, Abierto = GPIO HIGH

### Electroimán/Relay
- **GPIO25**: Salida hacia módulo relay o MOSFET
  - `RELAY_ACTIVE_LEVEL = 0` (Activo en bajo)
  - Relay module IN → GPIO25
  - Electroimán conectado a contacto NO/COM del relay

### Potenciómetro (Entrada de Combinación)
- **GPIO1** (ADC1_CH0):
  - Terminal izquierdo → GND
  - Terminal central (wiper) → GPIO1
  - Terminal derecho → 3.3V
  - Rango de lectura: 0-4095 (12 bits)

### LCD1602 I2C
- **GPIO21** (SDA) → SDA del módulo I2C
- **GPIO22** (SCL) → SCL del módulo I2C
- **VCC** → 5V
- **GND** → GND
- Dirección I2C: **0x27**

### MFRC522 RFID (SPI2)
- **GPIO12** (SCK) → SCK del módulo RFID
- **GPIO11** (MOSI) → MOSI del módulo RFID
- **GPIO13** (MISO) → MISO del módulo RFID
- **GPIO10** (CS) → SDA/SS del módulo RFID
- **GPIO9** (RST) → RST del módulo RFID
- **3.3V** → 3.3V del módulo
- **GND** → GND
- ⚠️ **IMPORTANTE**: Usar SPI2_HOST en lugar de SPI3_HOST

---

## 🚨 Pines Reservados en ESP32-S3 (NO USAR)

- **GPIO0**: Strapping pin (Boot mode)
- **GPIO3**: Strapping pin (USB-JTAG)
- **GPIO19-20**: USB-OTG (si está habilitado)
- **GPIO26-32**: PSRAM/Flash Octal (si está habilitado)
- **GPIO43-44**: UART0 TX/RX (consola debug)
- **GPIO45**: Strapping pin (VDD_SPI)
- **GPIO46**: Strapping pin (ROM boot mode)

---

## ⚙️ Configuración de Software

### WiFi/MQTT
```c
#define WIFI_SSID "iPhone de Cesar"
#define WIFI_PASS "DenGra9401"
#define MQTT_BROKER "mqtt://172.20.10.8:1883"
#define MQTT_TOPIC "iot/telemetry"
```

### Combinación de Acceso
```c
static const int COMBO_TARGET[3] = {3, 6, 4};
```

### UID Autorizado (RFID)
```c
static const uint8_t AUTH_UIDS[][4] = {
    {0xEA, 0xE8, 0xD2, 0x84}
};
```

---

## 🔧 Comandos de Compilación

### Configurar Target ESP32-S3
```powershell
idf.py set-target esp32s3
```

### Compilar
```powershell
idf.py build
```

### Flashear
```powershell
idf.py flash
```

### Monitor Serial
```powershell
idf.py monitor
```

### Todo en uno
```powershell
idf.py build flash monitor
```

---

## 📝 Notas Adicionales

1. **ADC Calibration**: El ESP32-S3 tiene mejor calibración de ADC que el ESP32 Classic. Puede que necesites ajustar `POT_MAP_SHIFT_RAW` después de probar con tu hardware.

2. **Flash Size**: El proyecto está configurado para 4MB de flash. Verifica que tu ESP32-S3 tenga al menos esa capacidad.

3. **PSRAM**: Si tu ESP32-S3 tiene PSRAM, puedes habilitarlo en `menuconfig` para más memoria.

4. **USB Serial**: El ESP32-S3 soporta USB serial nativo en GPIO19/20. Si lo usas, actualiza los pines UART.

5. **Power**: El ESP32-S3 consume más energía que el ESP32 Classic. Asegura una fuente de 5V/2A mínimo.

---

## 🐛 Solución de Problemas

### Error "GPIO number error"
- Verifica que no estés usando GPIO26-32, 33-39 (no existen en S3)
- Revisa que los pines no estén en conflicto

### RFID no funciona
- Verifica conexiones SPI2
- Asegúrate de usar módulo RFID de **3.3V** (no 5V)
- Revisa que SPI2_HOST esté configurado en el código

### ADC da valores erráticos
- Añade un capacitor de 100nF entre GPIO1 y GND
- Ajusta `POT_DEADZONE_RAW` si es necesario
- Usa `ADC_ATTEN_DB_11` para rango completo 0-3.3V

---

**Última actualización**: Migración completa a ESP32-S3
**Versión ESP-IDF**: 5.5.1
