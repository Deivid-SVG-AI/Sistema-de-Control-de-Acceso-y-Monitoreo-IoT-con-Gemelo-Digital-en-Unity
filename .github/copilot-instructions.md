# Copilot Instructions for this ESP-IDF Project

## Overview
This project is an ESP32 access control system built with ESP-IDF. It manages a magnetic door sensor, an electromagnetic lock, a buzzer, status LEDs, a rotary encoder (digits + confirm button), and an optional MFRC522 RFID reader. Core logic enforces a safety rule: only lock when the door is detected closed.

Key files:
- `main/main.c`: Hardware configuration, FreeRTOS tasks, and access logic
- `CMakeLists.txt` (root): Standard ESP-IDF project definition (minimal build)
- `main/CMakeLists.txt`: Component registration (see Gotchas for a filename fix)
- `README.md`: Hardware mapping, behavior, and usage instructions

## Architecture
- FreeRTOS tasks in `main/main.c`:
  - `door_monitor_task`: Tracks door state; triggers relock when closed; enforces max unlock time
  - `encoder_task`: Captures a 3-digit combination via encoder (CLK/DT) + confirm (SW)
  - `rfid_task`: Stub by default; activates `EVT_RFID_OK` when real MFRC522 support is added
  - `control_task`: Applies access mode (AND/OR), unlocks, and defers relock until door is closed
- Shared state and events:
  - Event bits: `EVT_RFID_OK`, `EVT_COMBO_OK`, `EVT_DOOR_CLOSED`, `EVT_LOCKED` (via `EventGroupHandle_t`)
  - Safety invariant: never lock while `g_door_state == DOOR_OPEN`
- Peripherals in `main/main.c`: LEDC buzzer utilities, LED helpers, GPIO setup, lock driver (`LOCK_ACTIVE_HIGH` aware)

## Build & Run (Windows PowerShell)
Prereq: ESP-IDF environment initialized in this shell.
```powershell
idf.py set-target esp32
idf.py build
idf.py flash
idf.py monitor
```
Exit monitor with `Ctrl+]`.

## Configuration & Conventions
- Edit hardware pins and timings at the top of `main/main.c` (CONFIGURACIÓN section)
  - Access mode: `#define ACCESS_MODE ACCESS_MODE_AND|ACCESS_MODE_OR`
  - Combination: `static const int COMBO_TARGET[3] = {3,1,4};`
  - Timers: `UNLOCK_MAX_OPEN_TIME_MS`, `INPUT_IDLE_RESET_MS`
  - Lock polarity: `LOCK_ACTIVE_HIGH` to match your relay/MOSFET
- Logging: use `ESP_LOGx` with tag `"ACCESS"`
- Feedback patterns: short beep on ticks, double beep on confirm, long beep on error
- Task priorities and pinning are set explicitly; keep consistent when adding tasks

## RFID Integration (MFRC522)
- Default disabled: `#define USE_MFRC522 0`
- To enable:
  1) Set `#define USE_MFRC522 1`
  2) Add an MFRC522 component from ESP-IDF Component Registry (SPI VSPI pins are defined)
  3) Implement init/read in `rfid_task()` and call `xEventGroupSetBits(g_events, EVT_RFID_OK)` for authorized UIDs
- Authorized UIDs: extend `AUTH_UIDS` in `main/main.c`

## Testing Notes
- `pytest_hello_world.py` is from the ESP-IDF template and asserts "Hello world!". The current app logs `"Sistema de Acceso..."` and task messages, so this test will fail as-is. Either remove/skip it or adapt assertions to this app’s logs.

## Gotchas
- `main/CMakeLists.txt` currently lists `hello_world_main.c`. Change to `main.c`:
  ```cmake
  idf_component_register(SRCS "main.c" PRIV_REQUIRES spi_flash INCLUDE_DIRS "")
  ```
- The system starts locked if the door is closed; if started with door open, it keeps lock drive active but will only truly lock once the door closes.
- Encoder logic resets partial input after `INPUT_IDLE_RESET_MS`; confirm each digit with `SW`.

## Common Changes (Examples)
- Switch to OR mode: set `#define ACCESS_MODE ACCESS_MODE_OR`
- Change combo to 7-0-7: set `COMBO_TARGET` to `{7,0,7}`
- Invert lock drive: set `#define LOCK_ACTIVE_HIGH 0`
