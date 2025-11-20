# Sistema de Acceso y Monitoreo de Seguridad (ESP32 + ESP-IDF)

Este proyecto implementa un control de acceso para una puerta, con registro por consola (logs), dos vías de autenticación y monitoreo del estado de la puerta y la cerradura. Pensado para una Maestría en IoT e IA, enfocado en el uso de un ESP32 con periféricos sencillos.

## Funcionalidad Actual
- Dos modos de autenticación:
	1. AND: se requiere tarjeta RFID válida y combinación correcta en el encoder.
	2. OR: se requiere tarjeta RFID válida o combinación correcta en el encoder.
- Sensor magnético de puerta (reed) para conocer estado: abierta/cerrada.
- Electroimán (cerradura) controlado por GPIO. Regla de seguridad: solo se puede bloquear (lock) cuando la puerta está detectada como cerrada.
- Buzzer para confirmar eventos (beeps por giro, confirmación, error, etc.).
- LEDs de estado: uno de sistema, uno verde (acceso concedido), uno rojo (bloqueado/error).
- Lógica de desbloqueo con tiempo máximo abierto: si la puerta no se abre, y está cerrada, se vuelve a bloquear cumplido el tiempo. Si está abierta, el sistema espera al cierre para poder bloquear (regla del sistema).

## Estado del soporte RFID (MFRC522)
- El código incluye una tarea `rfid_task` y toda la configuración de pines para un MFRC522 por SPI.
- Por defecto, el soporte está DESACTIVADO para compilar sin dependencias externas. Para activarlo, define la macro `USE_MFRC522 = 1` en `main/main.c` y agrega un componente/driver para MFRC522 compatible con ESP-IDF (por ejemplo, desde el ESP-IDF Component Registry). Completa las llamadas en `rfid_task()` según el driver elegido.
- Hasta que no se habilite MFRC522, el acceso solo puede completarse con la combinación (en modo OR) o no se otorgará por RFID (en modo AND no se concederá acceso sin RFID).

## “Potenciómetro” con pines CLK, DT, SW (Encoder rotatorio)
- Se usa un encoder tipo KY-040 (CLK, DT, SW). En el proyecto se usan:
	- CLK y DT: para incrementar/decrementar el dígito actual (0–9).
	- SW: para confirmar el dígito actual.
- ¿Por qué usar SW?: Confirmar cada dígito con un botón evita que un giro involuntario cambie el valor al momento de confirmar. Esto reduce errores de entrada.
- La combinación por defecto es de 3 dígitos, definida en `COMBO_TARGET` dentro de `main/main.c`.

## Pines por defecto (edítalos en la sección de CONFIGURACIÓN de `main/main.c`)
- Buzzer: `GPIO25` (LEDC PWM)
- LEDs: `LED_STATUS=GPIO22`, `LED_GREEN=GPIO2`, `LED_RED=GPIO21`
- Sensor magnético de puerta: `GPIO27` (input con pull-up)
- Electroimán (cerradura): `GPIO26` (salida). Macro `LOCK_ACTIVE_HIGH` define si nivel alto energiza el imán.
- Encoder: `CLK=GPIO32`, `DT=GPIO33`, `SW=GPIO13`
- MFRC522 (SPI VSPI): `SCK=GPIO18`, `MOSI=GPIO23`, `MISO=GPIO19`, `CS=GPIO5`, `RST=GPIO17`

Ajusta estos pines a tu hardware físico directamente en la parte superior de `main/main.c`.

## Parámetros clave
Modifica en `main/main.c`:
- `#define ACCESS_MODE ...` para alternar entre `ACCESS_MODE_AND` o `ACCESS_MODE_OR`.
- `COMBO_TARGET[3]` para cambiar la combinación.
- Tiempos: `UNLOCK_MAX_OPEN_TIME_MS`, `INPUT_IDLE_RESET_MS`.
- `LOCK_ACTIVE_HIGH` según tu relé/MOSFET/electroimán.

## Comportamiento (resumen)
- Inicio: LEDs se inicializan; el sistema arranca bloqueado (rojo). El LED verde se enciende cuando se desbloquea.
- Encoder:
	- Girar a derecha/izquierda cambia el dígito actual (beep corto).
	- Pulsar `SW` confirma el dígito (beep doble). Tras 3 confirmaciones se valida la combinación.
	- Si es correcta: se marca `COMBO_OK`. Si no, beep de error y se reinicia la captura.
	- Si hay inactividad (`INPUT_IDLE_RESET_MS`), se reinicia la captura parcial.
- RFID: si habilitado y la UID está en la lista, se marca `RFID_OK`.
- Control de acceso: según el modo, al cumplirse la condición se desbloquea (LED verde y beep de confirmación). Luego, el sistema vuelve a bloquear solo cuando detecta la puerta cerrada. Si la puerta no se abre y se mantiene cerrada, puede bloquear tras `UNLOCK_MAX_OPEN_TIME_MS`.
- Regla de seguridad: Nunca se intenta bloquear si la puerta está detectada como abierta; en ese caso, el sistema espera al cierre.

## Cómo compilar y grabar (Windows PowerShell)
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

## Cómo habilitar MFRC522 más adelante
1. Define `#define USE_MFRC522 1` en `main/main.c`.
2. Agrega un componente MFRC522 compatible (por ejemplo, desde el registry de ESP-IDF) y sus dependencias SPI.
3. Completa la inicialización y lectura en la función `rfid_task()` siguiendo la API de tu driver (detección de tarjeta, lectura de UID y comparación con `AUTH_UIDS`).

## Estructura principal del código
- `main/main.c`
	- Sección de CONFIGURACIÓN con pines y parámetros globales.
	- Buzzer (LEDC), LEDs, sensor de puerta, control de cerradura, encoder y RFID (stub/driver).
	- Tareas FreeRTOS:
		- `door_monitor_task`: Monitorea puerta y aplica la regla de bloqueo al cerrar.
		- `encoder_task`: Gestiona la entrada de combinación (3 dígitos con confirmación por SW).
		- `rfid_task`: Lee tarjeta y activa `EVT_RFID_OK` (stub por defecto).
		- `control_task`: Aplica la condición AND/OR y controla unlock/lock.

## Advertencias de hardware
- Verifica niveles lógicos y necesidades de corriente del electroimán: usa un relé o MOSFET adecuado con diodo de rueda libre si es DC.
- No uses pines de arranque (strapping) para señales críticas si no estás seguro. Los pines seleccionados evitan problemas comunes de boot.
- Ajusta la lógica del reed (abierto/cerrado) si tu cableado es diferente (ver `read_door_state()`).

## Próximos pasos sugeridos
- Persistir eventos (e.g., en NVS o enviar por MQTT).
- Interfaz física para alternar entre modos AND/OR.
- Integrar driver MFRC522 real y manejo de varias UIDs.
- Añadir debounce por interrupción o usando GPIO ISR si deseas menor latencia.

