# Sistema de Acceso y Monitoreo de Seguridad (ESP32 + ESP-IDF)

Proyecto de control de acceso con ESP32: puerta con sensor magnético, cerradura (servo o electroimán), buzzer, LEDs, autenticación por combinación (potenciómetro analógico) y/o tarjeta RFID MFRC522, más pantalla LCD1602 I2C para retroalimentación visual. Enfatiza la regla de seguridad: nunca bloquear mientras la puerta esté abierta.

## Resumen de Funcionalidad Actual
- Modos de autenticación: `ACCESS_MODE_AND` (RFID + combinación) o `ACCESS_MODE_OR` (RFID o combinación). Valor por defecto en el código: OR.
- Sensor magnético (reed) para estado puerta (abierta/cerrada).
- Cerradura accionada por servo (por defecto `LOCK_USE_SERVO=1`), o salida GPIO para electroimán si se cambia a `LOCK_USE_SERVO=0`.
- Buzzer (LEDC) con patrones: pip corto (tick), doble pip acceso correcto, triple pip combinación incorrecta, pip en lectura RFID.
- LEDs de estado: sistema (azul), verde acceso concedido, rojo denegado/bloqueado.
- Regla de seguridad: solo se ejecuta lock (cerrar) si la puerta está detectada como cerrada; se difiere el re‑lock 1 segundo al cerrar tras un unlock.
- Tiempo máximo desbloqueado si la puerta nunca se abrió (`UNLOCK_MAX_OPEN_TIME_MS`).
- Pantalla LCD1602 (I2C + PCF8574) para mensajes: idle, progreso combinación, granted/denied, locking, patrón de diagnóstico.

## Estado del soporte RFID (MFRC522)
- En el código actual `USE_MFRC522` está en `1`, lo que significa que se requiere el archivo/driver `mfrc522_min.h` (añádelo como componente). Si no tienes el driver, puedes poner `USE_MFRC522 0` para compilar sin RFID.
- Whitelist de UIDs en `AUTH_UIDS`: inicialmente contiene `{EA:E8:D2:84}`.
- Al detectar UID autorizado: set del bit `EVT_RFID_OK`, mensaje LCD “ACCESS GRANTED!” y doble beep (por la lógica general de acceso).

## Entrada de Combinación (Potenciómetro Analógico)
- Se sustituyó el encoder rotatorio por un potenciómetro conectado a `GPIO34` (ADC1_CH6).
- La lectura analógica (0..4095) se mapea a dígitos `0..10` (`POT_MAX_DIGIT=10`).
- El sistema captura un dígito cuando el valor permanece estable por `POT_SETTLE_MS` (1200 ms) tras movimiento.
- Longitud de combinación: `COMBO_LEN = 3`, objetivo: `{3,6,4}`.
- Al capturar cada dígito: beep corto y actualización LCD (“CURRENT PASS:” + progreso).
- Al completar 3 dígitos: comparación con `COMBO_TARGET`. Correcta => doble beep y `EVT_COMBO_OK`; incorrecta => triple pip, LED rojo breve y reset de la captura.

## Pines Actuales (ver sección CONFIGURACIÓN en `main/main.c`)
| Función | Macro / Definición | Pin |
|--------|--------------------|-----|
| Buzzer (LEDC) | `BUZZER_GPIO` | GPIO26 |
| LED sistema | `LED_STATUS_GPIO` | GPIO14 |
| LED verde | `LED_GREEN_GPIO` | GPIO12 |
| LED rojo | `LED_RED_GPIO` | GPIO27 |
| Sensor puerta (reed) | `DOOR_SENSOR_GPIO` | GPIO33 |
| Servo / Lock GPIO | `SERVO_GPIO` / `LOCK_GPIO` | GPIO25 |
| Potenciómetro ADC | `POT_ADC_GPIO` (ADC1_CH6) | GPIO34 |
| MFRC522 SCK | `RFID_SPI_SCK_GPIO` | GPIO18 |
| MFRC522 MOSI | `RFID_SPI_MOSI_GPIO` | GPIO23 |
| MFRC522 MISO | `RFID_SPI_MISO_GPIO` | GPIO19 |
| MFRC522 CS | `RFID_SPI_CS_GPIO` | GPIO5 |
| MFRC522 RST | `RFID_RST_GPIO` | GPIO13 |
| LCD I2C SDA | `I2C_SDA_GPIO` | GPIO21 |
| LCD I2C SCL | `I2C_SCL_GPIO` | GPIO22 |

Nota: reemplaza servo por electroimán ajustando `LOCK_USE_SERVO` y cableado; si usas electroimán, verifica `LOCK_ACTIVE_HIGH` según tu transistor/relevador.

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
 Servo Signal (lock) ---+ GPIO25 (SERVO / LOCK_GPIO)           |
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
- Servo: fuente externa 5V (GND común con ESP32) si el consumo supera lo que el regulador puede entregar.
- MFRC522: 3.3V (evitar 5V directo si el módulo no regula internamente).
- LCD1602: muchos módulos aceptan 5V (backlight más brillante); si usas 5V asegura niveles I2C tolerados o adapta.
```

Leyenda:
- Flechas (^) indican entrada al ESP32; líneas simples representan señal directa.
- Mantener cables I2C (SDA/SCL) cortos y separados de líneas de potencia de servo para reducir ruido.

## Parámetros Clave
- `ACCESS_MODE` define lógica AND/OR.
- `UNLOCK_MAX_OPEN_TIME_MS` máximo tiempo desbloqueado sin abrir puerta.
- `COMBO_TARGET[]` combinación objetivo.
- `LOCK_ACTIVE_HIGH` polaridad hardware del lock si no es servo.
- `POT_SETTLE_MS` estabilidad requerida para capturar dígito.
- `POT_MAX_DIGIT` rango de dígitos (0..10).

## Comportamiento (Resumen Operativo)
1. Arranque: inicializa periféricos, LCD muestra patrón de diagnóstico si `LCD_DEBUG_PATTERN=1`, luego estado idle.
2. Puerta cerrada al inicio: se bloquea inmediatamente (lock). Puerta abierta: se energiza lock pero sólo se confirma cierre cuando reed reporta `CERRADA`.
3. Combinación: cada dígito se captura tras estabilizarse; al fallar se reinicia secuencia.
4. RFID: al escanear tarjeta autorizada se satisface condición de acceso según modo.
5. Desbloqueo: si se cumplen condiciones (AND u OR) y la puerta está cerrada, se hace `unlock_door()`. Si está abierta, espera cierre.
6. Re-bloqueo: al cerrar después de un unlock, se arma retardo de 1s antes de ejecutar lock.
7. Tiempo máximo desbloqueado: si nunca se abrió, se intenta lock sólo si puerta está cerrada.

## LCD1602 (I2C + PCF8574)
- Dirección confirmada: `0x27`, variante de mapeo de pines: `VARIANT 0`.
- Frecuencia I2C reducida a 50 kHz para mayor margen ante ruido.
- Debug inicial opcional (`LCD_DEBUG_PATTERN`) imprime caracteres de prueba para descartar errores de mapeo.
- Auto-probe disponible si se habilita `LCD_AUTOPROBE` (recorre variantes 0..5 y direcciones 0x27/0x3F para diagnóstico visual). Desactivado por defecto.
- Cada actualización de mensaje limpia la pantalla y reposiciona el cursor en (0,0) y (0,1) para evitar corrimientos.

## Partición Flash
- `sdkconfig` está configurado para usar una tabla de particiones custom (`partitions.csv`). Contiene `nvs`, `factory` y `spiffs`.
- Para volver a tabla por defecto: `idf.py menuconfig` → Partition Table → seleccionar “Single factory app” y desactivar custom CSV.

## Cómo Compilar y Grabar (Windows PowerShell)
```powershell
idf.py set-target esp32
idf.py build
idf.py flash
idf.py monitor
```
Salir del monitor: `Ctrl+]`.

### Entorno ESP-IDF (Windows)
Si `idf.py` no funciona, exporta entorno:
```powershell
& "$env:IDF_PATH/export.ps1"
```
O abre la terminal “ESP-IDF PowerShell”. Verifica:
```powershell

## Cómo compilar y grabar (Windows PowerShell)
```

## Habilitar / Deshabilitar MFRC522
1. Ajusta `USE_MFRC522` (1 habilita, 0 deshabilita).
2. Añade componente/driver y asegúrate de que `mfrc522_min.h` esté accesible.
3. Expande lógica de `rfid_task()` si requieres anticollision extendida o autenticación.

## Estructura Principal del Código
- `door_monitor_task`: estado de puerta, armado de re-lock diferido.
- `pot_task`: lectura estable de potenciómetro, captura dígitos y validación de combinación.
- `rfid_task`: lectura de tarjeta, comparación whitelist y set de evento.
- `control_task`: espera eventos (AND/OR), desbloquea y limpia banderas.
- `lcd_task`: render periódico de buffer de mensajes.
- Funciones de lock/unlock aplican regla de seguridad (no lock con puerta abierta).

## Advertencias de Hardware
- Servo: alimentarlo separado o asegurar corriente suficiente; evitar ruido que afecte I2C.
- Electroimán: usar driver MOSFET + diodo flyback; no alimentar directo del pin.
- Pull-ups I2C: verificar que el módulo LCD tenga resistencias (frecuencia reducida ayuda si hay cables largos).
- Reed: adaptar lógica en `read_door_state()` si cableado tiene niveles invertidos.

## Próximos Pasos Sugeridos
- Persistencia (NVS) de últimos accesos y UIDs.
- Comunicación (MQTT/HTTP) para reportar eventos.
- OTA con particiones duales si se requiere actualización segura.
- Integrar autenticación adicional (PIN por keypad matricial, BLE, etc.).
- Watchdog y métricas de rendimiento (tiempo de tareas, latencias).

## Eventos y Bits
- `EVT_RFID_OK`: tarjeta autorizada.
- `EVT_COMBO_OK`: combinación correcta.
- `EVT_DOOR_CLOSED`: puerta confirmada cerrada.
- `EVT_LOCKED`: estado bloqueado actual.

## Patrones de Sonido (Resumen)
- Tick corto: movimiento/captura dígito o escaneo RFID.
- Doble pip: combinación correcta / acceso concedido.
- Triple pip: combinación incorrecta.
- Pip largo (error): puede emplearse para otros fallos futuros.

## Diagrama Lógico de Eventos y Flujo
```
								+---------------------------+
								|        door_monitor_task  |
								| reads reed every 50 ms    |
								+-------------+-------------+
															| sets/clears
										EVT_DOOR_CLOSED (closed/open)
															|                 (arms re-lock 1s after close
															|                  if g_pending_relock=true)
															v
				+------------------ control_task ------------------+
				|  WAIT CONDITION:                                 |
				|  OR  => (EVT_RFID_OK OR EVT_COMBO_OK)            |
				|  AND => (EVT_RFID_OK AND EVT_COMBO_OK)           |
				+-----------+------------------+-------------------+
										| bits satisfied
										v
						(if door open => wait EVT_DOOR_CLOSED)
										|
										v
							 unlock_door()
										| sets g_pending_relock=true
										| clears EVT_RFID_OK / EVT_COMBO_OK
										v
				(User may open door -> EVT_DOOR_CLOSED cleared)
										|
			Upon subsequent close + 1s delay (armed) by door_monitor
										v
							 lock_door() -> sets EVT_LOCKED, resets combo
										|
										v
								 BACK TO IDLE

OTHER EVENT PRODUCERS:

 +------------------+          +------------------+
 |    rfid_task     |          |    pot_task      |
 | reads MFRC522    |          | ADC stable read  |
 | new authorized   |          | captures digit   |
 | UID => set       |          | builds sequence  |
 | EVT_RFID_OK      |          | when sequence OK |
 +---------+--------+          | set EVT_COMBO_OK |
					 |                   +---------+--------+
					 |                               |
					 +---------------+---------------+
													 |
													 v (consumed by control_task condition logic)

TIMEOUT PATH:
	If UNLOCKED and door never opened for UNLOCK_MAX_OPEN_TIME_MS:
		 - If door closed => lock_door()
		 - Else => wait until door closes then normal re-lock path.

LCD TASK RELATION:
	lcd_task renders messages set by pot_task, rfid_task, lock/unlock functions.
	It does NOT generate events; it reacts to state changes.

STATE FLAGS SUMMARY:
	g_lock_state: LOCKED/UNLOCKED (informational + LED logic)
	g_pending_relock: true after unlock until lock_door()
	g_relock_arm_time_us: timestamp for delayed re-lock after close.

SECUENCIA DE ACCESO EXITOSO (OR ejemplo):
	Idle -> Usuario ingresa 3 dígitos correctos (EVT_COMBO_OK) ->
	control_task detecta condición -> (puerta cerrada?) sí -> unlock_door ->
	Usuario abre puerta (EVT_DOOR_CLOSED cleared) -> Usuario cierra puerta ->
	door_monitor arma re-lock +1s -> lock_door -> Idle.

SECUENCIA FALLIDA DE COMBINACIÓN:
	Captura dígitos -> comparación incorrecta -> triple pip, LED rojo, reset combinación -> Idle sin EVT_COMBO_OK.
```

---
Mantén `main/main.c` como fuente de verdad; actualiza este README si cambias pines, tiempos o modo de autenticación.
Asegúrate de tener ESP-IDF configurado. Luego en la raíz del proyecto:

```powershell
idf.py set-target esp32
idf.py build
idf.py flash
idf.py monitor
```

Para salir del monitor: `Ctrl+]`.

### Configuración del entorno ESP-IDF (Windows)
Si `idf.py` no se reconoce como comando, la sesión PowerShell no tiene cargado el entorno de ESP-IDF.

Opciones para inicializarlo:
1. Abrir el acceso directo "ESP-IDF PowerShell" que crea el instalador (recomendado). Esto abre una consola ya preparada.
2. Ejecutar manualmente el script `export.ps1` dentro de tu carpeta de ESP-IDF. Ejemplos (ajusta versión/ruta):
	```powershell
	# Si instalaste con el instalador oficial:
	& "C:\Espressif\frameworks\esp-idf-v5.2\export.ps1"

	# Si clonaste el repo tú mismo:
	cd C:\Users\LEONI\esp-idf
	.\install.ps1   # (solo la primera vez para instalar herramientas)
	.\export.ps1    # cada nueva terminal para cargar entorno
	```
3. Verifica que las variables estén definidas:
	```powershell
	echo $env:IDF_PATH
	echo $env:IDF_TOOLS_PATH
	where idf.py
	```

Tras esto, vuelve a los comandos `idf.py set-target esp32`, `idf.py build`, etc.

<!-- Secciones antiguas sobre encoder y pines obsoletos reemplazadas por la configuración actual -->

