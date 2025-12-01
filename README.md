# Sistema de Acceso y Monitoreo de Seguridad (ESP32-S3 + ESP-IDF)

> **⚠️ IMPORTANTE**: Este proyecto ha sido migrado a **ESP32-S3**. Si tienes un ESP32 Classic, consulta el historial de Git para la versión anterior. Ver **[MIGRATION_SUMMARY.md](MIGRATION_SUMMARY.md)** y **[ESP32_S3_PIN_MAP.md](ESP32_S3_PIN_MAP.md)** para detalles de la migración.

Proyecto de control de acceso con ESP32-S3: puerta con sensor magnético, cerradura electromagnética, buzzer, LEDs, autenticación por combinación (potenciómetro analógico), tarjeta RFID MFRC522 y control remoto vía MQTT, más pantalla LCD1602 I2C para retroalimentación visual. Enfatiza la regla de seguridad: nunca bloquear mientras la puerta esté abierta.

## Resumen de Funcionalidad Actual

### Métodos de Acceso
El sistema implementa **tres métodos independientes de acceso**:
1. **Combinación por Potenciómetro**: Entrada de 3 dígitos mediante potenciómetro analógico (combinación predeterminada: 3-6-4)
2. **Tarjeta RFID (MFRC522)**: Autenticación mediante tarjetas NFC autorizadas
3. **Control Remoto MQTT**: Desbloqueo remoto a través de comandos JSON vía MQTT en el topic `iot/commands`

### Modos de Autenticación
- **`ACCESS_MODE_AND`**: Requiere RFID + combinación simultáneamente
- **`ACCESS_MODE_OR`**: Acepta RFID O combinación O comando remoto (modo predeterminado)
- El acceso remoto MQTT **siempre concede acceso** independientemente del modo configurado

### Hardware de Control
- **Cerradura electromagnética** controlada por relay (por defecto `LOCK_USE_SERVO=0`)
  - Estado BLOQUEADO: relay inactivo (bobina desenergizada) - estado de reposo seguro
  - Estado DESBLOQUEADO: relay activo (bobina energizada) - permite apertura de puerta
  - Configuración de polaridad: `RELAY_ACTIVE_LEVEL=0` (relay activo en LOW)
- **Alternativa servo**: Disponible cambiando `LOCK_USE_SERVO=1` (ángulos configurables)

### Monitoreo y Retroalimentación
- **Sensor magnético (reed switch)**: Detecta estado de puerta (abierta/cerrada)
- **Buzzer (PWM LEDC)** con patrones diferenciados:
  - Pip corto: captura de dígito o detección de tarjeta
  - Doble pip: acceso concedido
  - Triple pip: combinación incorrecta
- **LEDs de estado**:
  - Azul (GPIO14): sistema listo (siempre encendido)
  - Verde (GPIO12): acceso concedido / desbloqueado
  - Rojo (GPIO27): acceso denegado (parpadeo breve)
- **Pantalla LCD1602 (I2C + PCF8574)**: 
  - Mensajes contextuales: idle, progreso de combinación, granted/denied, locking
  - Dirección I2C: 0x27 (variante de mapeo: VARIANT 0)
  - Frecuencia reducida a 50 kHz para estabilidad

### Conectividad IoT
- **WiFi Station Mode**: Conexión automática con reconexión (SSID: "iPhone de Cesar")
- **Cliente MQTT**: 
  - Broker: `mqtt://172.20.10.8:1883`
  - Topic de telemetría: `iot/telemetry` (publica eventos en formato JSON)
  - Topic de comandos: `iot/commands` (suscrito para control remoto)
- **Sistema de Logs SPIFFS**: Registro persistente de eventos en `/spiffs/events.jsonl`
  - Campos: `device_id`, `door_status`, `access_method`, `access_granted`, `timestamp`
  - Formato: JSON Lines (un evento por línea)

### Reglas de Seguridad
- **Lock solo con puerta cerrada**: La cerradura NUNCA se bloquea si el sensor detecta puerta abierta
- **Re-lock diferido**: Después de desbloquear, espera 1 segundo tras detectar cierre de puerta antes de bloquear automáticamente
- **Timeout de desbloqueo**: Si la puerta no se abre tras `UNLOCK_MAX_OPEN_TIME_MS` (10 segundos), intenta re-bloquear solo si está cerrada
- **Comportamiento del lock NO automático**: La cerradura NO cambia de estado cuando la puerta se abre/cierra; solo responde a:
  - Métodos de acceso exitosos (RFID, combinación, remoto)
  - Timer de re-lock después de acceso concedido
  - Timeout máximo de desbloqueo

## Estado del Soporte RFID (MFRC522)
- **Habilitado por defecto**: `USE_MFRC522=1` (requiere archivo/driver `mfrc522_min.h`)
- **Whitelist de UIDs autorizados**: Configurada en `AUTH_UIDS` con UID inicial `{EA:E8:D2:84}`
- **Funcionamiento**: 
  - Detección automática de tarjetas cada 150ms
  - Validación contra whitelist
  - Al detectar UID autorizado: establece bit `EVT_RFID_OK`, muestra "ACCESS GRANTED!" en LCD y doble beep
  - Beep corto al detectar cualquier tarjeta (autorizada o no)
- **Para compilar sin RFID**: Cambiar a `USE_MFRC522 0` en `main/main.c`

## Entrada de Combinación (Potenciómetro Analógico)
- **Hardware**: Potenciómetro conectado a `GPIO34` (ADC1_CH6)
- **Mapeo de valores**: 
  - Lectura ADC: 0..4095 (12 bits)
  - Se mapea linealmente a dígitos: 0..10 (`POT_MAX_DIGIT=10`)
- **Captura de dígitos**:
  - El sistema captura un dígito cuando el valor permanece **estable** por `POT_SETTLE_MS` (1200 ms) tras movimiento
  - Requiere movimiento del potenciómetro antes de capturar el siguiente dígito (evita capturas duplicadas)
  - Longitud de combinación: `COMBO_LEN = 3`
  - Combinación objetivo: `{3,6,4}` (modificable en código)
- **Retroalimentación**:
  - Beep corto al capturar cada dígito
  - Actualización LCD: muestra "CURRENT PASS:" con progreso
  - Al completar 3 dígitos:
    - **Correcta**: doble beep, `EVT_COMBO_OK` activado, LED verde
    - **Incorrecta**: triple pip, LED rojo breve, reset automático de captura
- **Reset automático**: Si no hay movimiento por `INPUT_IDLE_RESET_MS` (8 segundos), limpia combinación parcial

## Control Remoto MQTT
- **Topic de suscripción**: `iot/commands`
- **Formato de comando**: JSON con cualquiera de estas estructuras:
  ```json
  {"action": "open"}
  {"open": true}
  {}
  ```
- **Comportamiento**:
  - Cualquier JSON válido sin campos específicos se interpreta como solicitud de desbloqueo
  - Activa el bit `EVT_REMOTE_OK` inmediatamente
  - Registra el evento en logs con `access_method: "remote"`
  - Publica confirmación vía MQTT en topic de telemetría
- **Prioridad**: El acceso remoto siempre concede acceso, independiente del modo AND/OR configurado

## Pines Actuales (ver sección CONFIGURACIÓN en `main/main.c`)
| Función | Macro / Definición | Pin |
|--------|--------------------|-----|
| Buzzer (LEDC) | `BUZZER_GPIO` | GPIO26 |
| LED sistema | `LED_STATUS_GPIO` | GPIO14 |
| LED verde | `LED_GREEN_GPIO` | GPIO12 |
| LED rojo | `LED_RED_GPIO` | GPIO27 |
| Sensor puerta (reed) | `DOOR_SENSOR_GPIO` | GPIO33 |
| Relay / Lock GPIO | `LOCK_GPIO` | GPIO25 |
| Potenciómetro ADC | `POT_ADC_GPIO` (ADC1_CH6) | GPIO34 |
| MFRC522 SCK | `RFID_SPI_SCK_GPIO` | GPIO18 |
| MFRC522 MOSI | `RFID_SPI_MOSI_GPIO` | GPIO23 |
| MFRC522 MISO | `RFID_SPI_MISO_GPIO` | GPIO19 |
| MFRC522 CS | `RFID_SPI_CS_GPIO` | GPIO5 |
| MFRC522 RST | `RFID_RST_GPIO` | GPIO13 |
| LCD I2C SDA | `I2C_SDA_GPIO` | GPIO21 |
| LCD I2C SCL | `I2C_SCL_GPIO` | GPIO22 |

**Notas sobre hardware**:
- **Cerradura**: Por defecto usa relay electromagnético (`LOCK_USE_SERVO=0`). Para servo, cambiar a `LOCK_USE_SERVO=1`
- **Polaridad relay**: `RELAY_ACTIVE_LEVEL=0` (activo en LOW). Ajustar según tu módulo relay/transistor
- **Reed switch**: Configurado con pull-up interno; contacto a GND indica puerta cerrada

## Diagrama Textual de Conexiones
```
                        +---------------- ESP32 ----------------+
                        |                                      |
        (Door Reed) ----+ GPIO33 (DOOR_SENSOR)       3V3        |
                        |    ^ (pull-up interno)     |         |
                        |    |                       |         |
                        |    |                  +----+----+    |
                        |    |                  |  LCD1602 |   |
                        |    |   I2C SDA -------+ GPIO21   |   |
                        |    |   I2C SCL -------+ GPIO22   |   |
                        |    |                  | Addr 0x27|   |
                        |    |                  +----+----+    |
                        |    |                       |BL       |
                        |    |                       v         |
 Relay Electromagnético + GPIO25 (LOCK_GPIO, activo LOW)       |
 (MOSFET + diodo)       |                                      |
                        |                                      |
 Buzzer PWM ----------- + GPIO26 (BUZZER)                      |
                        |                                      |
 LED Azul (Status) -----+ GPIO14                               |
 LED Verde (Granted) ---+ GPIO12                               |
 LED Rojo (Denied)  ----+ GPIO27                               |
                        |                                      |
 Potenciómetro wiper ---+ GPIO34 (ADC1_CH6)                    |
 (Otros extremos a 3V3 y GND)                                   |
                        |                                      |
 SPI MFRC522:            |  SCK=GPIO18  MOSI=GPIO23             |
                         |  MISO=GPIO19 CS=GPIO5 RST=GPIO13     |
                        +--------------------------------------+

Alimentación recomendada:
- Relay electromagnético: Si usa bobina de 5V, alimentar directamente; usar MOSFET NPN/PNP para control desde GPIO25
- MFRC522: 3.3V (evitar 5V directo si el módulo no regula internamente)
- LCD1602: muchos módulos aceptan 5V (backlight más brillante); si usas 5V asegura niveles I2C tolerados o adapta
```

Leyenda:
- Flechas (^) indican entrada al ESP32; líneas simples representan señal directa
- **IMPORTANTE**: Usar diodo flyback en paralelo con bobina del relay para proteger el MOSFET
- Mantener cables I2C (SDA/SCL) cortos y separados de líneas de potencia del relay para reducir ruido

## Parámetros Clave de Configuración
- **`ACCESS_MODE`**: Define lógica de autenticación (AND/OR)
- **`UNLOCK_MAX_OPEN_TIME_MS`**: Tiempo máximo desbloqueado sin abrir puerta (10 segundos)
- **`INPUT_IDLE_RESET_MS`**: Timeout para reset de combinación parcial (8 segundos)
- **`COMBO_TARGET[]`**: Combinación objetivo de 3 dígitos
- **`RELAY_ACTIVE_LEVEL`**: Polaridad del relay (0 = activo en LOW, 1 = activo en HIGH)
- **`POT_SETTLE_MS`**: Tiempo de estabilidad para capturar dígito (1200 ms)
- **`POT_MAX_DIGIT`**: Rango de dígitos del potenciómetro (0..10)
- **WiFi/MQTT**: 
  - `WIFI_SSID` / `WIFI_PASS`: Credenciales de red
  - `MQTT_BROKER`: URI del broker MQTT
  - `MQTT_TOPIC`: Topic de telemetría

## Comportamiento Operativo Completo

### 1. Arranque del Sistema
- Inicializa todos los periféricos (GPIO, I2C, ADC, SPI, WiFi, MQTT)
- Monta sistema de archivos SPIFFS para logs
- LCD muestra patrón de diagnóstico si `LCD_DEBUG_PATTERN=1` está habilitado
- Conecta a WiFi y establece conexión MQTT
- Lee estado inicial de puerta:
  - **Puerta cerrada**: bloquea cerradura inmediatamente (relay inactivo)
  - **Puerta abierta**: mantiene relay activo pero NO considera bloqueado hasta detectar cierre
- Muestra mensaje de bienvenida en LCD: "WELCOME, INPUT PASSWORD OR RFID"

### 2. Captura de Combinación (Potenciómetro)
- Monitorea continuamente lectura ADC del potenciómetro
- Mapea valor 0..4095 a dígitos 0..10
- **Captura de dígito**:
  - Detecta movimiento del potenciómetro
  - Espera estabilización por 1200 ms
  - Captura dígito cuando valor se mantiene estable
  - Emite beep corto y actualiza LCD con progreso
- **Validación de secuencia**:
  - Al completar 3 dígitos, compara con `COMBO_TARGET`
  - **Correcta**: doble beep, activa `EVT_COMBO_OK`, LED verde
  - **Incorrecta**: triple pip, flash de LED rojo, limpia captura
- **Reset automático**: Limpia combinación parcial tras 8 segundos de inactividad

### 3. Autenticación RFID
- Escanea tarjetas cada 150 ms
- Al detectar tarjeta nueva:
  - Emite beep corto
  - Lee UID y compara contra whitelist `AUTH_UIDS`
  - **Autorizada**: activa `EVT_RFID_OK`, muestra "ACCESS GRANTED!", doble beep
  - **No autorizada**: solo beep de detección, sin conceder acceso
- Previene lecturas repetidas del mismo UID mientras la tarjeta permanece presente

### 4. Control Remoto MQTT
- Escucha topic `iot/commands` continuamente
- Al recibir mensaje:
  - Parsea JSON del payload
  - Valida campos: `"action":"open"` o `"open":true` o JSON vacío
  - **Válido**: activa `EVT_REMOTE_OK` inmediatamente
  - Registra evento en logs y SPIFFS
  - Publica confirmación en `iot/telemetry`
- **Sin validación de puerta**: el comando remoto SIEMPRE concede acceso independiente del estado de puerta

### 5. Lógica de Control de Acceso (`control_task`)
- Espera eventos de los 3 métodos de acceso: `EVT_RFID_OK | EVT_COMBO_OK | EVT_REMOTE_OK`
- **Evaluación según modo configurado**:
  - **Modo AND**: Requiere `EVT_RFID_OK` Y `EVT_COMBO_OK` simultáneamente
  - **Modo OR**: Acepta cualquier método individual
  - **Excepción**: `EVT_REMOTE_OK` SIEMPRE concede acceso (prioridad máxima)
- **Proceso de desbloqueo**:
  - Si puerta está abierta: espera evento `EVT_DOOR_CLOSED` antes de proceder
  - Llama a `unlock_door()`: energiza relay (bobina activa), LED verde ON
  - Marca `g_pending_relock = true` para armar re-lock futuro
  - Limpia bits de eventos consumidos
- **Registro de evento**: Escribe JSON a SPIFFS y publica vía MQTT con método de acceso usado

### 6. Monitoreo de Puerta (`door_monitor_task`)
- Lee sensor reed cada 50 ms con debounce
- **Detección de cambios de estado**:
  - **ABIERTA → CERRADA**:
    - Establece bit `EVT_DOOR_CLOSED`
    - Registra evento en logs: `access_method:"door"`, `door_status:"close"`
    - Si `g_pending_relock == true`: arma timer de re-lock diferido (1 segundo)
    - **NO cambia estado del relay** (comportamiento modificado)
  - **CERRADA → ABIERTA**:
    - Limpia bit `EVT_DOOR_CLOSED`
    - Registra evento: `door_status:"open"`
    - Cancela timer de re-lock si estaba armado
    - **NO cambia estado del relay** (comportamiento modificado)
- **Re-lock diferido**: 
  - Después de cerrar la puerta tras desbloqueo, espera 1 segundo
  - Llama a `lock_door()`: desenergiza relay (bobina OFF), LED verde OFF
  - Solo ejecuta si puerta sigue cerrada al cumplirse el timer
- **Timeout de desbloqueo**: 
  - Si estado es UNLOCKED y puerta no se abrió por 10 segundos
  - Intenta `lock_door()` solo si `g_door_state == DOOR_CLOSED`

### 7. Actualización de LCD (`lcd_task`)
- Renderiza buffer de mensajes cada 100 ms
- Limpia pantalla y reposiciona cursor en cada actualización (evita artefactos)
- **Auto-clear por inactividad**: Vuelve a mensaje idle tras 5 segundos sin actividad
- **Mensaje "LOCKING"**: Se mantiene visible por 1 segundo tras bloquear

### 8. Telemetría y Logs
- **Eventos registrados**:
  - Cambios de estado de puerta (open/close)
  - Intentos de acceso (exitosos y fallidos)
  - Comandos remotos (aceptados y rechazados)
- **Formato de log JSON**:
  ```json
  {
    "device_id": "access_control_01",
    "door_status": "open|close",
    "access_method": "password|rfid|remote|door",
    "access_granted": true|false,
    "timestamp": ""
  }
  ```
- **Destinos**:
  - Archivo local: `/spiffs/events.jsonl` (JSON Lines, un evento por línea)
  - MQTT: Publicación en `iot/telemetry` con QoS 1
- **Campo timestamp**: Intencionalmente vacío por requerimiento del proyecto

## LCD1602 (I2C + PCF8574)
- Dirección confirmada: `0x27`, variante de mapeo de pines: `VARIANT 0`.
- Frecuencia I2C reducida a 50 kHz para mayor margen ante ruido.
- Debug inicial opcional (`LCD_DEBUG_PATTERN`) imprime caracteres de prueba para descartar errores de mapeo.
- Auto-probe disponible si se habilita `LCD_AUTOPROBE` (recorre variantes 0..5 y direcciones 0x27/0x3F para diagnóstico visual). Desactivado por defecto.
- Cada actualización de mensaje limpia la pantalla y reposiciona el cursor en (0,0) y (0,1) para evitar corrimientos.

## Partición Flash
- `sdkconfig` está configurado para usar una tabla de particiones custom (`partitions.csv`). Contiene `nvs`, `factory` y `spiffs`.
- Para volver a tabla por defecto: `idf.py menuconfig` → Partition Table → seleccionar "Single factory app" y desactivar custom CSV.

## Cómo Compilar y Grabar (Windows PowerShell)

### Opción 1: Script Automático (Recomendado)
```powershell
.\build_esp32s3.ps1
```

### Opción 2: Comandos Manuales
```powershell
# Configurar target para ESP32-S3
idf.py set-target esp32s3

# Compilar
idf.py build

# Flashear
idf.py flash

# Monitor serial
idf.py monitor
```
Salir del monitor: `Ctrl+]`.

### Entorno ESP-IDF (Windows)
Si `idf.py` no funciona, exporta entorno:
```powershell
& "$env:IDF_PATH/export.ps1"
```

> **📖 Documentación adicional**: Ver **[ESP32_S3_PIN_MAP.md](ESP32_S3_PIN_MAP.md)** para mapeo completo de pines y **[MIGRATION_SUMMARY.md](MIGRATION_SUMMARY.md)** para cambios desde ESP32 Classic.
O abre la terminal "ESP-IDF PowerShell". Verifica:
```powershell
echo $env:IDF_PATH
where idf.py
```

## Arquitectura de Tareas FreeRTOS

El sistema utiliza 5 tareas concurrentes con prioridades diferenciadas:

| Tarea | Función | Prioridad | Descripción |
|-------|---------|-----------|-------------|
| `control_task` | Control de acceso | 7 (máxima) | Evalúa condiciones de acceso y ejecuta unlock/lock |
| `door_monitor_task` | Monitoreo de puerta | 6 | Lee sensor reed, gestiona re-lock diferido y timeouts |
| `pot_task` | Entrada de combinación | 5 | Lee ADC, captura dígitos, valida secuencia |
| `rfid_task` | Lector RFID | 4 | Escanea tarjetas, valida UIDs contra whitelist |
| `lcd_task` | Actualización de display | 3 | Renderiza mensajes en LCD1602 |

### Sincronización mediante Event Groups
- **Event Bits**:
  - `EVT_RFID_OK` (bit 0): Tarjeta autorizada detectada
  - `EVT_COMBO_OK` (bit 1): Combinación correcta ingresada
  - `EVT_DOOR_CLOSED` (bit 2): Puerta detectada cerrada
  - `EVT_LOCKED` (bit 3): Estado de cerradura bloqueada
  - `EVT_REMOTE_OK` (bit 4): Comando remoto MQTT recibido

### Variables de Estado Globales
- `g_door_state`: Estado actual de puerta (DOOR_OPEN / DOOR_CLOSED / DOOR_UNKNOWN)
- `g_lock_state`: Estado de cerradura (LOCKED / UNLOCKED / LOCK_STATE_UNKNOWN)
- `g_pending_relock`: Flag para armar re-lock tras desbloqueo
- `g_relock_arm_time_us`: Timestamp del timer de re-lock diferido
- `g_entered[]`: Buffer de dígitos capturados del potenciómetro
- `g_entered_count`: Contador de dígitos ingresados
- `g_mqtt_client`: Handle del cliente MQTT

## Estructura Principal del Código (`main/main.c`)

### Funciones Clave
- **`lock_door()`**: Desenergiza relay (bobina OFF), solo si puerta cerrada; limpia banderas de acceso
- **`unlock_door()`**: Energiza relay (bobina ON), establece `g_pending_relock = true`
- **`combo_reset()`**: Limpia buffer de combinación y limpia `EVT_COMBO_OK`
- **`log_event()`**: Registra evento en SPIFFS y publica vía MQTT
- **`mqtt_event_handler()`**: Maneja conexión MQTT, suscripción a topics y comandos remotos
- **`wifi_event_handler()`**: Gestiona reconexión automática de WiFi

### Inicialización en `app_main()`
1. NVS Flash init (requerido para WiFi)
2. WiFi/MQTT setup y conexión
3. Creación de Event Group
4. Inicialización de periféricos (GPIO, I2C, ADC, SPI, LEDC, SPIFFS)
5. Lectura de estado inicial de puerta
6. Creación de las 5 tareas FreeRTOS

## Configuración de Hardware Avanzada

### Habilitar / Deshabilitar MFRC522
1. Ajusta `USE_MFRC522` (1 habilita, 0 deshabilita) en `main/main.c`
2. Si está habilitado, añade componente/driver y asegúrate de que `mfrc522_min.h` esté accesible
3. Para extender funcionalidad: modifica lógica de `rfid_task()` para anticollision extendida o autenticación de sectores

## Estructura Principal del Código
- `door_monitor_task`: estado de puerta, armado de re-lock diferido.
- `pot_task`: lectura estable de potenciómetro, captura dígitos y validación de combinación.
- `rfid_task`: lectura de tarjeta, comparación whitelist y set de evento.
- `control_task`: espera eventos (AND/OR), desbloquea y limpia banderas.
- `lcd_task`: render periódico de buffer de mensajes.
- Funciones de lock/unlock aplican regla de seguridad (no lock con puerta abierta).

### Advertencias de Seguridad y Hardware
- **Relay electromagnético**: 
  - Usar driver MOSFET (NPN o PNP según diseño)
  - **OBLIGATORIO**: Diodo flyback (1N4007 o similar) en paralelo con bobina para proteger MOSFET
  - NO conectar bobina directo a GPIO (corriente excesiva daña ESP32)
  - Verificar `RELAY_ACTIVE_LEVEL` según tipo de módulo relay (muchos son activos en LOW)
- **Potencia**:
  - Relay de 5V: alimentar desde fuente externa, GND común con ESP32
  - Corriente típica de bobina: 30-70mA (excede límite de GPIO de 12mA)
- **I2C (LCD)**:
  - Verificar que módulo PCF8574 tenga resistencias pull-up (2.2kΩ - 4.7kΩ)
  - Frecuencia reducida a 50 kHz ayuda con cables largos (> 15cm)
  - Evitar proximidad con líneas de potencia del relay (ruido EMI)
- **Reed switch**: 
  - Configurado con pull-up interno del ESP32
  - Contacto a GND indica puerta cerrada
  - Adaptar lógica en `read_door_state()` si tu cableado usa lógica invertida
- **RFID MFRC522**: 
  - Alimentar SOLO con 3.3V (no tolera 5V directo en muchos módulos)
  - Mantener cables SPI cortos (< 10cm recomendado)
  - Condensador 100nF entre VCC y GND cerca del módulo ayuda con estabilidad

## Partición Flash y Sistema de Archivos
- **Tabla de particiones custom** (`partitions.csv`):
  - `nvs`: 24KB para almacenamiento WiFi/calibración
  - `factory`: Aplicación principal
  - `storage`: 128KB para SPIFFS (logs de eventos)
- **Sistema SPIFFS**:
  - Montado en `/spiffs/`
  - Archivo de logs: `/spiffs/events.jsonl`
  - Auto-format si falla montaje
  - Mutex `g_log_mutex` protege escrituras concurrentes
- **Para volver a tabla por defecto**: 
  ```bash
  idf.py menuconfig
  # → Partition Table → Single factory app (no custom CSV)
  ```

## Eventos y Bits de Estado

| Bit | Macro | Descripción | Productor | Consumidor |
|-----|-------|-------------|-----------|------------|
| 0 | `EVT_RFID_OK` | Tarjeta autorizada detectada | `rfid_task` | `control_task` |
| 1 | `EVT_COMBO_OK` | Combinación correcta ingresada | `pot_task` | `control_task` |
| 2 | `EVT_DOOR_CLOSED` | Puerta confirmada cerrada | `door_monitor_task` | `control_task` |
| 3 | `EVT_LOCKED` | Estado de cerradura bloqueada | `lock_door()` / `unlock_door()` | — |
| 4 | `EVT_REMOTE_OK` | Comando remoto MQTT recibido | `mqtt_event_handler()` | `control_task` |

## Patrones de Retroalimentación Sonora

| Patrón | Duración | Significado | Contexto |
|--------|----------|-------------|----------|
| **Beep corto** | 30ms | Movimiento detectado / Captura de dígito / Tarjeta detectada | Potenciómetro o RFID |
| **Doble pip** | 80ms + pausa + 80ms | Acceso concedido / Combinación correcta | Autenticación exitosa |
| **Triple pip** | 3x 30ms con pausas | Combinación incorrecta | Fallo de combinación |
| **Beep largo** | 300ms | Error general (reservado) | Futuros errores |

## Próximos Pasos y Mejoras Sugeridas
- **Persistencia NVS**: 
  - Almacenar histórico de accesos y UIDs autorizados
  - Configuración WiFi/MQTT modificable sin recompilar
- **OTA (Over-The-Air)**:
  - Implementar particiones duales para actualizaciones seguras
  - Rollback automático si nueva versión falla
- **Autenticación adicional**:
  - Keypad matricial 4x4 para PIN numérico
  - Bluetooth LE para desbloqueo desde smartphone
  - Biométrico (huella digital) con sensor AS608
- **Monitoreo avanzado**:
  - SNTP para timestamps reales (actualmente vacíos)
  - Métricas de latencia de tareas (uso de `esp_timer`)
  - Watchdog para recuperación ante cuelgues
- **Seguridad**:
  - Encriptación de logs en SPIFFS
  - TLS/SSL para conexión MQTT
  - Rate limiting en comandos remotos (anti-brute force)
- **Interfaz web**:
  - Servidor HTTP embebido para configuración
  - Dashboard en tiempo real con WebSockets
  - Gestión de whitelist RFID vía web

## Diagrama Lógico de Eventos y Flujo
```
                                +---------------------------+
                                |    door_monitor_task      |
                                |  Lee reed cada 50 ms      |
                                +-------------+-------------+
                                              | establece/limpia
                                    EVT_DOOR_CLOSED (cerrada/abierta)
                                              | (arma re-lock 1s después de cerrar
                                              |  si g_pending_relock=true)
                                              v
                +------------------- control_task --------------------+
                |  CONDICIÓN DE ESPERA:                               |
                |  OR  => (EVT_RFID_OK | EVT_COMBO_OK | EVT_REMOTE_OK)|
                |  AND => (EVT_RFID_OK & EVT_COMBO_OK)                |
                |  Excepción: EVT_REMOTE_OK siempre concede acceso    |
                +------------+-------------------+---------------------+
                             | bits satisfechos
                             v
              (si puerta abierta => espera EVT_DOOR_CLOSED)
                             |
                             v
                      unlock_door()
                             | establece g_pending_relock=true
                             | limpia EVT_RFID_OK / EVT_COMBO_OK / EVT_REMOTE_OK
                             v
         (Usuario puede abrir puerta -> EVT_DOOR_CLOSED limpiado)
                             |
  Al cerrar subsecuentemente + delay 1s (armado) por door_monitor
                             v
                 lock_door() -> establece EVT_LOCKED, resetea combo
                             |
                             v
                      VUELTA A IDLE

PRODUCTORES DE EVENTOS:

 +------------------+  +------------------+  +----------------------+
 |   rfid_task      |  |    pot_task      |  | mqtt_event_handler   |
 | Lee MFRC522      |  | Lectura ADC      |  | Parsea JSON remote   |
 | UID autorizado   |  | Captura dígito   |  | Comando válido =>    |
 | => EVT_RFID_OK   |  | Secuencia OK =>  |  | EVT_REMOTE_OK        |
 +--------+---------+  | EVT_COMBO_OK     |  +----------+-----------+
          |            +---------+--------+             |
          |                      |                      |
          +----------+-----------+-----------+----------+
                     |                       |
                     v (consumidos por control_task)

RUTA DE TIMEOUT:
  Si UNLOCKED y puerta nunca abierta por UNLOCK_MAX_OPEN_TIME_MS:
    - Si puerta cerrada => lock_door()
    - Si no => espera cierre y luego ruta normal de re-lock

RELACIÓN LCD TASK:
  lcd_task renderiza mensajes establecidos por pot_task, rfid_task, funciones lock/unlock
  NO genera eventos; reacciona a cambios de estado

RESUMEN DE FLAGS DE ESTADO:
  g_lock_state: LOCKED/UNLOCKED (informacional + lógica de LEDs)
  g_pending_relock: true después de unlock hasta lock_door()
  g_relock_arm_time_us: timestamp para re-lock diferido tras cerrar
  g_door_state: DOOR_OPEN/DOOR_CLOSED/DOOR_UNKNOWN

SECUENCIA DE ACCESO EXITOSO (modo OR, ejemplo):
  Idle -> Usuario ingresa 3 dígitos correctos (EVT_COMBO_OK) ->
  control_task detecta condición -> (¿puerta cerrada?) sí -> unlock_door() ->
  Usuario abre puerta (EVT_DOOR_CLOSED limpiado) -> Usuario cierra puerta ->
  door_monitor arma re-lock +1s -> lock_door() -> Idle

SECUENCIA FALLIDA DE COMBINACIÓN:
  Captura dígitos -> comparación incorrecta -> triple pip, LED rojo, 
  reset combinación -> Idle sin EVT_COMBO_OK

SECUENCIA DE ACCESO REMOTO:
  Broker MQTT publica {"action":"open"} en iot/commands ->
  mqtt_event_handler() parsea JSON -> EVT_REMOTE_OK activado ->
  control_task concede acceso inmediato (sin validar puerta) ->
  unlock_door() -> LED verde ON
```

---

## Resumen de Cambios Recientes
1. **Control remoto MQTT**: Añadido método de acceso vía comandos JSON en topic `iot/commands`
2. **Comportamiento de lock modificado**: La cerradura NO cambia automáticamente cuando la puerta se abre/cierra; solo responde a métodos de acceso o timers
3. **Sistema de logs SPIFFS**: Registro persistente de todos los eventos con formato JSON Lines
4. **WiFi/MQTT integrado**: Telemetría en tiempo real y control remoto IoT
5. **Timestamp vacío**: Campo timestamp en logs intencionalmente dejado como cadena vacía

## Notas Finales
- **Fuente de verdad**: `main/main.c` es el código definitivo; actualiza este README si modificas pines, tiempos o modos
- **Configuración WiFi/MQTT**: Editar credenciales en `main/main.c` antes de compilar
- **Requisito ESP-IDF**: Versión 5.x recomendada (proyecto desarrollado con v5.5.1)
- **Logs locales**: Archivos en `/spiffs/events.jsonl` persisten entre reinicios; considerar rotación si el uso es intensivo

---

Mantén este documento actualizado al realizar cambios en la arquitectura o funcionalidad del sistema.

