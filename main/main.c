/*
 * Proyecto: Sistema de Acceso y Monitoreo de Seguridad (ESP32 + ESP-IDF)
 * Descripción general:
 *   - Control de acceso mediante tarjeta RFID (MFRC522) y/o combinación con un
 *     encoder rotatorio (popularmente llamado “potenciómetro” con pines CLK, DT, SW).
 *   - Sensor magnético de puerta para conocer estado abierta/cerrada.
 *   - Electroimán (cerradura) controlado por GPIO. Solo se puede BLOQUEAR (lock)
 *     cuando la puerta está detectada como CERRADA.
 *   - Buzzer para retroalimentación sonora y LEDs de estado.
 *   - Dos modos de acceso:
 *       1) AND: Se requiere tarjeta RFID Y combinación correcta.
 *       2) OR:  Se requiere tarjeta RFID O combinación correcta.
 *
 * Notas importantes:
 *   - En esta versión, el soporte para MFRC522 está listo para activarse si
 *     se integra una librería/componente del RC522. Por defecto está desactivado
 *     para que el proyecto compile sin dependencias externas. Véase README.
 *   - Todo el mapeo de pines y parámetros se concentra en la sección “CONFIGURACIÓN”.
 *
 * Lenguaje de comentarios: Español, con foco educativo/explicativo.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "sdkconfig.h"  // Define CONFIG_* Kconfig macros (e.g., CONFIG_FREERTOS_HZ, CONFIG_LOG_MAXIMUM_LEVEL)
#ifndef CONFIG_FREERTOS_HZ
// Fallback for IntelliSense when ESP-IDF environment not loaded yet.
#define CONFIG_FREERTOS_HZ 100
#endif
#ifndef CONFIG_LOG_MAXIMUM_LEVEL
// Default typical log level (Info). Adjust via menuconfig once environment is active.
#define CONFIG_LOG_MAXIMUM_LEVEL 3
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "rom/ets_sys.h"

// =============================================================
// ===============   CONFIGURACIÓN (EDITABLE)   ================
// =============================================================

// Modo de acceso:
//   1 = AND (RFID y combinación)
//   2 = OR  (RFID o combinación)
#define ACCESS_MODE_AND 1
#define ACCESS_MODE_OR  2
#define ACCESS_MODE      ACCESS_MODE_OR    // Cambiar aquí para el modo deseado

// Tiempos clave (en ms)
#define UNLOCK_MAX_OPEN_TIME_MS   10000  // Tiempo máximo que permanecerá desbloqueada si la puerta no se abre
#define INPUT_IDLE_RESET_MS        8000  // Tiempo de inactividad del encoder para resetear la captura
#define DEBOUNCE_MS                 40   // Anti-rebote para entradas digitales

// Configuración del buzzer (LEDC PWM)
#define BUZZER_GPIO               GPIO_NUM_26
#define BUZZER_LEDC_TIMER         LEDC_TIMER_0
#define BUZZER_LEDC_MODE          LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_CHANNEL       LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ            2000   // Frecuencia del beep

// LEDs de estado
#define LED_STATUS_GPIO           GPIO_NUM_14  // LED de “sistema listo”
#define LED_GREEN_GPIO            GPIO_NUM_12   // LED de acceso concedido
#define LED_RED_GPIO              GPIO_NUM_27  // LED de acceso denegado / bloqueado

// Sensor magnético de puerta (reed switch)
#define DOOR_SENSOR_GPIO          GPIO_NUM_33  // Usar con pull-up interno y contacto a GND

// Electroimán (cerradura) controlado por un MOSFET/Relay externo
#define LOCK_GPIO                 GPIO_NUM_25
#define LOCK_ACTIVE_HIGH          1            // 1 si “1 lógico” ENERGIZA el imán (lock). 0 si al revés.

// Alternativa: Servo como cerradura (selección por compilación)
// 0 = electroimán (LOCK_GPIO), 1 = servo (SERVO_GPIO)
#define LOCK_USE_SERVO            1
#define SERVO_GPIO                GPIO_NUM_25   // Cambia según tu hardware
#define SERVO_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_TIMER          LEDC_TIMER_1
#define SERVO_LEDC_CHANNEL        LEDC_CHANNEL_1
#define SERVO_FREQ_HZ             50           // SG90: 50 Hz (periodo ~20 ms)
// Usa mayor resolución para mejor precisión de pulso
#define SERVO_TIMER_RES           LEDC_TIMER_14_BIT
#define SERVO_TIMER_BITS          14
// SG90 típico: ~500us (0°) a ~2400us (180°) — ajusta si tu servo necesita otro rango
#define SERVO_MIN_US              500
#define SERVO_MAX_US              2400
// Ángulos para lock/unlock (ajusta según mecánica de tu cerradura)
#define SERVO_LOCK_DEG            90
#define SERVO_UNLOCK_DEG          0

// Potenciómetro analógico (sustituye encoder). Usamos solo GPIO34 (ADC1_CH6)
#define POT_ADC_GPIO              GPIO_NUM_34

// Resolución esperada ADC (ESP32 ADC1 es 12 bits por defecto => 0..4095)
#define POT_ADC_MAX_RAW           4095
// Mapeo lineal: 0 voltaje -> dígito 0, máximo -> dígito 10 (11 niveles)
#define POT_MAX_DIGIT             10
// Tiempo que debe permanecer estable el valor para considerar un dígito (ms)
#define POT_SETTLE_MS             1200
// Mínimo tiempo entre logs del potenciómetro mientras se mueve (ms)
#define POT_LOG_MIN_MS            300
// Longitud de la combinación (número de dígitos a ingresar)
#define COMBO_LEN                 3
// Combinación objetivo (ejemplo)
static const int COMBO_TARGET[COMBO_LEN] = {3, 6, 4};

// Tarjeta RFID MFRC522 (SPI). Pines VSPI por defecto del ESP32.
#define RFID_SPI_CS_GPIO          GPIO_NUM_5
#define RFID_SPI_SCK_GPIO         GPIO_NUM_18
#define RFID_SPI_MOSI_GPIO        GPIO_NUM_23
#define RFID_SPI_MISO_GPIO        GPIO_NUM_19
#define RFID_RST_GPIO             GPIO_NUM_13

#define USE_MFRC522               1

#if USE_MFRC522
static const uint8_t AUTH_UIDS[][4] = {
    {0xEA, 0xE8, 0xD2, 0x84}
};
static const size_t AUTH_UIDS_COUNT = sizeof(AUTH_UIDS)/sizeof(AUTH_UIDS[0]);
#endif

// Eliminada la combinación multi-dígito (encoder). Ahora el evento EVT_COMBO_OK
// se activa llevando el potenciómetro al máximo (dígito 10).

// =============================================================
// ============   VARIABLES GLOBALES DEL SISTEMA   =============
// =============================================================

static const char *TAG = "ACCESS";

typedef enum {
	DOOR_UNKNOWN = 0,
	DOOR_OPEN,
	DOOR_CLOSED
} door_state_t;

typedef enum {
	LOCK_STATE_UNKNOWN = 0,
	LOCKED,
	UNLOCKED
} lock_state_t;

// Evento/flags compartidos por tareas
static EventGroupHandle_t g_events;
#define EVT_RFID_OK      (1<<0)
#define EVT_COMBO_OK     (1<<1)
#define EVT_DOOR_CLOSED  (1<<2)
#define EVT_LOCKED       (1<<3)

// Estado de alto nivel
static volatile door_state_t g_door_state = DOOR_UNKNOWN;
static volatile lock_state_t g_lock_state = LOCK_STATE_UNKNOWN;
static volatile bool g_pending_relock = false;
static int64_t g_relock_arm_time_us = 0;

// Potenciómetro estado
static volatile int g_current_digit = 0;
static int64_t g_last_pot_print_us = 0;
static volatile int g_entered[COMBO_LEN] = {0};
static volatile int g_entered_count = 0;
static int64_t g_last_move_ts_us = 0;
static bool g_digit_captured_after_settle = false;

// Adelantar prototipo para reiniciar combinación
static void combo_reset(void);
// Prototipo de buzzer usado antes de definición
static void buzzer_play_ms(uint32_t ms, uint32_t duty);
// =============================================================
// ====================   LCD1602 (I2C)   =====================
// =============================================================

#define I2C_PORT                  I2C_NUM_0
#define I2C_SDA_GPIO              GPIO_NUM_21
#define I2C_SCL_GPIO              GPIO_NUM_22
#define I2C_FREQ_HZ               50000  // Reducido para mayor margen frente a ruido
#define LCD_ADDR                  0x27  // Confirmado por usuario
// Auto-probe deshabilitado (ya identificamos mapeo correcto)
#ifndef LCD_AUTOPROBE
#define LCD_AUTOPROBE 0
#endif

// PCF8574 pin mapping variants
// VARIANT 0 (muy común): P0=RS, P1=RW, P2=EN, P3=BL, P4..P7=D4..D7
// VARIANT 1 (algunas placas): P0..P3=D4..D7, P4=BL, P5=EN, P6=RW, P7=RS
// VARIANT 2: P0..P3=D4..D7, P4=EN, P5=RW, P6=RS, P7=BL
// VARIANT 3: P0=RS, P1=RW, P2=EN, P3=BL, P4..P7=D7..D4 (inversión D-lines)
#ifndef LCD_PINMAP_VARIANT
#define LCD_PINMAP_VARIANT 0
#endif

#if LCD_PINMAP_VARIANT==0
#define LCD_BL   0x08
#define LCD_EN   0x04
#define LCD_RW   0x02
#define LCD_RS   0x01
// Convierte nibble alto (D7..D4 en bits7..4) a bus P4..P7
static inline uint8_t lcd_pack_nibble(uint8_t hi_nibble, bool rs)
{
	uint8_t data = (hi_nibble & 0xF0) | LCD_BL | (rs ? LCD_RS : 0);
	return data;
}
#elif LCD_PINMAP_VARIANT==1
#define LCD_BL   0x10
#define LCD_EN   0x20
#define LCD_RW   0x40
#define LCD_RS   0x80
// Convierte nibble alto (D7..D4 en bits7..4) a bus P0..P3
static inline uint8_t lcd_pack_nibble(uint8_t hi_nibble, bool rs)
{
	uint8_t lower = (hi_nibble >> 4) & 0x0F; // D7..D4 -> P3..P0
	uint8_t data = lower | LCD_BL | (rs ? LCD_RS : 0);
	return data;
}
#elif LCD_PINMAP_VARIANT==2
#define LCD_BL   0x80
#define LCD_EN   0x10
#define LCD_RW   0x20
#define LCD_RS   0x40
// P0..P3 = D4..D7; P4=EN; P5=RW; P6=RS; P7=BL
static inline uint8_t lcd_pack_nibble(uint8_t hi_nibble, bool rs)
{
	uint8_t lower = (hi_nibble >> 4) & 0x0F;
	uint8_t data = lower | LCD_EN | LCD_BL | (rs ? LCD_RS : 0);
	return data;
}
#elif LCD_PINMAP_VARIANT==3
#define LCD_BL   0x08
#define LCD_EN   0x04
#define LCD_RW   0x02
#define LCD_RS   0x01
// Igual que 0 pero D-lines invertidas: P4..P7 = D4..D7 reversed (rare)
static inline uint8_t lcd_pack_nibble(uint8_t hi_nibble, bool rs)
{
	// Reverse D7..D4 order onto P4..P7: map D7->P4, D6->P5, D5->P6, D4->P7
	uint8_t d7 = (hi_nibble & 0x80) ? 0x10 : 0x00;
	uint8_t d6 = (hi_nibble & 0x40) ? 0x20 : 0x00;
	uint8_t d5 = (hi_nibble & 0x20) ? 0x40 : 0x00;
	uint8_t d4 = (hi_nibble & 0x10) ? 0x80 : 0x00;
	uint8_t data = d7 | d6 | d5 | d4 | LCD_BL | (rs ? LCD_RS : 0);
	return data;
}

#elif LCD_PINMAP_VARIANT==4
// VARIANT 4: P0..P3 = D7..D4 (reversed on low nibble), P4=BL, P5=EN, P6=RW, P7=RS
#define LCD_BL   0x10
#define LCD_EN   0x20
#define LCD_RW   0x40
#define LCD_RS   0x80
static inline uint8_t lcd_pack_nibble(uint8_t hi_nibble, bool rs)
{
	// Map D7..D4 -> P0..P3 (reversed)
	uint8_t p0 = (hi_nibble & 0x80) ? 0x01 : 0x00; // D7->P0
	uint8_t p1 = (hi_nibble & 0x40) ? 0x02 : 0x00; // D6->P1
	uint8_t p2 = (hi_nibble & 0x20) ? 0x04 : 0x00; // D5->P2
	uint8_t p3 = (hi_nibble & 0x10) ? 0x08 : 0x00; // D4->P3
	uint8_t data = p0 | p1 | p2 | p3 | LCD_BL | (rs ? LCD_RS : 0);
	return data;
}

#elif LCD_PINMAP_VARIANT==5
// VARIANT 5: P0..P3 = D7..D4 (reversed on low nibble), P4=EN, P5=RW, P6=RS, P7=BL
#define LCD_BL   0x80
#define LCD_EN   0x10
#define LCD_RW   0x20
#define LCD_RS   0x40
static inline uint8_t lcd_pack_nibble(uint8_t hi_nibble, bool rs)
{
	// Map D7..D4 -> P0..P3 (reversed)
	uint8_t p0 = (hi_nibble & 0x80) ? 0x01 : 0x00; // D7->P0
	uint8_t p1 = (hi_nibble & 0x40) ? 0x02 : 0x00; // D6->P1
	uint8_t p2 = (hi_nibble & 0x20) ? 0x04 : 0x00; // D5->P2
	uint8_t p3 = (hi_nibble & 0x10) ? 0x08 : 0x00; // D4->P3
	uint8_t data = p0 | p1 | p2 | p3 | LCD_EN | LCD_BL | (rs ? LCD_RS : 0);
	return data;
}
#else
#error "Unsupported LCD_PINMAP_VARIANT"
#endif

static SemaphoreHandle_t g_lcd_mutex;
static char g_lcd_line1[17] = {0};
static char g_lcd_line2[17] = {0};
static volatile bool g_lcd_dirty = false;
static int64_t g_last_activity_us = 0;
static volatile int64_t g_locking_until_us = 0;

static esp_err_t i2c_bus_init(void)
{
	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = I2C_SDA_GPIO,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_io_num = I2C_SCL_GPIO,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = I2C_FREQ_HZ,
		.clk_flags = 0,
	};
	ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
	return i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t lcd_write_byte(uint8_t val)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (LCD_ADDR << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, val, true);
	i2c_master_stop(cmd);
	esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
	i2c_cmd_link_delete(cmd);
	return err;
}

// Versión con dirección variable (para auto-probe)
static esp_err_t pcf8574_write_addr(uint8_t addr, uint8_t val)
{
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
	i2c_master_write_byte(cmd, val, true);
	i2c_master_stop(cmd);
	esp_err_t err = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
	i2c_cmd_link_delete(cmd);
	return err;
}

static void lcd_pulse_enable(uint8_t data)
{
	lcd_write_byte(data | LCD_EN);
	ets_delay_us(5);
	lcd_write_byte(data & ~LCD_EN);
	ets_delay_us(50);
}

static void lcd_write4(uint8_t nibble, bool rs)
{
	uint8_t data = lcd_pack_nibble(nibble, rs);
	lcd_write_byte(data & ~(LCD_RW)); // asegurar RW=0
	lcd_pulse_enable(data & ~(LCD_RW));
	ets_delay_us(100); // margen adicional
}

static void lcd_send(uint8_t value, bool rs)
{
    lcd_write4(value & 0xF0, rs);         // nibble alto
    lcd_write4((value << 4) & 0xF0, rs);   // nibble bajo
    ets_delay_us(200); // estabilizar tras byte completo
}

static inline void lcd_cmd(uint8_t cmd) { lcd_send(cmd, false); }
static inline void lcd_data(uint8_t data) { lcd_send(data, true); }

static void lcd_clear(void)
{
	lcd_cmd(0x01);
	vTaskDelay(pdMS_TO_TICKS(3));
	lcd_cmd(0x02); // HOME para garantizar cursor en 0
	vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_home(void)
{
	lcd_cmd(0x02);
	vTaskDelay(pdMS_TO_TICKS(2));
}

static void lcd_set_cursor(uint8_t col, uint8_t row)
{
	static const uint8_t row_addr[] = {0x00, 0x40, 0x14, 0x54};
	if (row > 1) row = 1;
	lcd_cmd(0x80 | (row_addr[row] + col));
}

static void lcd_print_len(const char *s, size_t maxlen)
{
	for (size_t i=0; i<maxlen; ++i) {
		char c = s[i];
		if (!c) break;
		lcd_data((uint8_t)c);
	}
}

static void lcd_init(void)
{
    i2c_bus_init();
    vTaskDelay(pdMS_TO_TICKS(120));
    // Secuencia idéntica a auto-probe
    lcd_write4(0x30, false); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write4(0x30, false); vTaskDelay(pdMS_TO_TICKS(2));
    lcd_write4(0x30, false); vTaskDelay(pdMS_TO_TICKS(2));
    lcd_write4(0x20, false); vTaskDelay(pdMS_TO_TICKS(2)); // Entrar a 4-bit
    lcd_cmd(0x28); // 2 líneas, 5x8
    lcd_cmd(0x0C); // Display ON
    lcd_cmd(0x06); // Entry mode
    lcd_clear();
    g_lcd_mutex = xSemaphoreCreateMutex();
}

#ifndef LCD_DEBUG_PATTERN
#define LCD_DEBUG_PATTERN 1
#endif

static void lcd_debug_pattern(void)
{
	// Patrón de diagnóstico para verificar mapeo estable sin interferencias
	lcd_clear();
	lcd_set_cursor(0,0); lcd_print_len("ADDR27 VAR0 OK", 16);
	lcd_set_cursor(0,1); lcd_print_len("ABCDEFGHIJKLMN", 16);
	vTaskDelay(pdMS_TO_TICKS(1200));
	lcd_clear();
	lcd_set_cursor(0,0); lcd_print_len("0123456789.,:?", 16);
	lcd_set_cursor(0,1); lcd_print_len("RF=READY POT=OK", 16);
	vTaskDelay(pdMS_TO_TICKS(1500));
}

#if LCD_AUTOPROBE
// ================= Auto-probe visual en arranque =================
// Construye el byte segun variante (0..5) para un nibble alto D7..D4
static inline uint8_t pack_by_variant_runtime(int variant, uint8_t hi_nibble, bool rs)
{
	switch (variant) {
		case 0: { // P0=RS,P1=RW,P2=EN,P3=BL,P4..P7=D4..D7
			uint8_t data = (hi_nibble & 0xF0) | 0x08 | (rs ? 0x01 : 0);
			return data;
		}
		case 1: { // P0..P3=D4..D7, P4=BL, P5=EN, P6=RW, P7=RS
			uint8_t lower = (hi_nibble >> 4) & 0x0F;
			uint8_t data = lower | 0x10 | (rs ? 0x80 : 0);
			return data;
		}
		case 2: { // P0..P3=D4..D7, P4=EN, P5=RW, P6=RS, P7=BL
			uint8_t lower = (hi_nibble >> 4) & 0x0F;
			uint8_t data = lower | 0x10 | 0x80 | (rs ? 0x40 : 0);
			return data;
		}
		case 3: { // P0=RS,P1=RW,P2=EN,P3=BL,P4..P7=D7..D4
			uint8_t d7 = (hi_nibble & 0x80) ? 0x10 : 0x00;
			uint8_t d6 = (hi_nibble & 0x40) ? 0x20 : 0x00;
			uint8_t d5 = (hi_nibble & 0x20) ? 0x40 : 0x00;
			uint8_t d4 = (hi_nibble & 0x10) ? 0x80 : 0x00;
			uint8_t data = d7 | d6 | d5 | d4 | 0x08 | (rs ? 0x01 : 0);
			return data;
		}
		case 4: { // P0..P3=D7..D4, P4=BL, P5=EN, P6=RW, P7=RS
			uint8_t p0 = (hi_nibble & 0x80) ? 0x01 : 0x00;
			uint8_t p1 = (hi_nibble & 0x40) ? 0x02 : 0x00;
			uint8_t p2 = (hi_nibble & 0x20) ? 0x04 : 0x00;
			uint8_t p3 = (hi_nibble & 0x10) ? 0x08 : 0x00;
			uint8_t data = p0 | p1 | p2 | p3 | 0x10 | (rs ? 0x80 : 0);
			return data;
		}
		case 5: { // P0..P3=D7..D4, P4=EN, P5=RW, P6=RS, P7=BL
			uint8_t p0 = (hi_nibble & 0x80) ? 0x01 : 0x00;
			uint8_t p1 = (hi_nibble & 0x40) ? 0x02 : 0x00;
			uint8_t p2 = (hi_nibble & 0x20) ? 0x04 : 0x00;
			uint8_t p3 = (hi_nibble & 0x10) ? 0x08 : 0x00;
			uint8_t data = p0 | p1 | p2 | p3 | 0x10 | 0x80 | (rs ? 0x40 : 0);
			return data;
		}
		default:
			return 0x00;
	}
}

static void lcd_pulse_enable_addr(uint8_t addr, uint8_t data, int variant)
{
	// EN mask per variant
	uint8_t en_mask = 0;
	switch (variant) {
		case 0: case 3: en_mask = 0x04; break;      // P2
		case 1: case 4: en_mask = 0x20; break;      // P5
		case 2: case 5: en_mask = 0x10; break;      // P4
	}
	pcf8574_write_addr(addr, data | en_mask);
	ets_delay_us(5);
	pcf8574_write_addr(addr, data & ~en_mask);
	ets_delay_us(50);
}

static void lcd_write4_addr(uint8_t addr, uint8_t nibble, bool rs, int variant)
{
	uint8_t data = pack_by_variant_runtime(variant, nibble, rs);
	pcf8574_write_addr(addr, data);
	lcd_pulse_enable_addr(addr, data, variant);
}

static void lcd_send_addr(uint8_t addr, uint8_t value, bool rs, int variant)
{
	lcd_write4_addr(addr, value & 0xF0, rs, variant);
	lcd_write4_addr(addr, (value << 4) & 0xF0, rs, variant);
}

static inline void lcd_cmd_addr(uint8_t addr, uint8_t cmd, int variant){ lcd_send_addr(addr, cmd, false, variant); }
static inline void lcd_data_addr(uint8_t addr, uint8_t data, int variant){ lcd_send_addr(addr, data, true, variant); }

static void lcd_clear_addr(uint8_t addr, int variant){ lcd_cmd_addr(addr, 0x01, variant); vTaskDelay(pdMS_TO_TICKS(2)); }
static void lcd_set_cursor_addr(uint8_t addr, uint8_t col, uint8_t row, int variant){ static const uint8_t row_addr[] = {0x00,0x40,0x14,0x54}; if(row>1) row=1; lcd_cmd_addr(addr, 0x80 | (row_addr[row] + col), variant);} 

static void lcd_print_addr(uint8_t addr, const char* s, int variant)
{
	for (; *s; ++s) lcd_data_addr(addr, (uint8_t)*s, variant);
}

static void lcd_probe_show(uint8_t addr, int variant)
{
	// Secuencia de init 4-bit por dirección/variante
	lcd_write4_addr(addr, 0x30, false, variant); vTaskDelay(pdMS_TO_TICKS(5));
	lcd_write4_addr(addr, 0x30, false, variant); vTaskDelay(pdMS_TO_TICKS(1));
	lcd_write4_addr(addr, 0x30, false, variant); vTaskDelay(pdMS_TO_TICKS(1));
	lcd_write4_addr(addr, 0x20, false, variant); vTaskDelay(pdMS_TO_TICKS(1));
	lcd_cmd_addr(addr, 0x28, variant);
	lcd_cmd_addr(addr, 0x0C, variant);
	lcd_cmd_addr(addr, 0x06, variant);
	lcd_clear_addr(addr, variant);
	char l1[17]; snprintf(l1, sizeof(l1), "ADDR %02X VAR %d", addr, variant);
	lcd_set_cursor_addr(addr, 0, 0, variant); lcd_print_addr(addr, l1, variant);
	lcd_set_cursor_addr(addr, 0, 1, variant); lcd_print_addr(addr, "HELLO 1602", variant);
}

static void lcd_autoprobe_run(void)
{
	ESP_LOGI(TAG, "LCD auto-probe iniciado");
	i2c_bus_init();
	vTaskDelay(pdMS_TO_TICKS(100));
	const uint8_t addrs[] = {0x27, 0x3F};
	for (size_t ai=0; ai<sizeof(addrs); ++ai) {
		uint8_t addr = addrs[ai];
		for (int v=0; v<=5; ++v) {
			ESP_LOGI(TAG, "Probe addr 0x%02X var %d", addr, v);
			lcd_probe_show(addr, v);
			// beep cortito para marcar cambio
			buzzer_play_ms(60, 300);
			vTaskDelay(pdMS_TO_TICKS(1500));
		}
	}
	ESP_LOGI(TAG, "LCD auto-probe terminado");
}
#endif // LCD_AUTOPROBE

static void lcd_set_message(const char *l1, const char *l2)
{
	if (!g_lcd_mutex) return;
	xSemaphoreTake(g_lcd_mutex, portMAX_DELAY);
	snprintf(g_lcd_line1, sizeof(g_lcd_line1), "%-16.16s", l1 ? l1 : "");
	snprintf(g_lcd_line2, sizeof(g_lcd_line2), "%-16.16s", l2 ? l2 : "");
	g_lcd_dirty = true;
	xSemaphoreGive(g_lcd_mutex);
}

static void touch_activity(void)
{
	g_last_activity_us = esp_timer_get_time();
}

static void lcd_show_idle(void)
{
	lcd_set_message("WELCOME, INPUT", "PASSWORD OR RFID");
}

static void lcd_task(void *arg)
{
	const int64_t IDLE_TIMEOUT_US = 5000000; // 5s
	for (;;) {
		int64_t now = esp_timer_get_time();
		// Manejo de LOCKING...
		if (g_locking_until_us > 0 && now >= g_locking_until_us) {
			g_locking_until_us = 0;
			lcd_show_idle();
		}

		// Mostrar idle si no hay actividad por 5s y no estamos mostrando progreso de pass
		if ((now - g_last_activity_us) >= IDLE_TIMEOUT_US) {
			lcd_show_idle();
			// Evitar que se vuelva a setear innecesariamente en cada ciclo
			g_last_activity_us = now; // marca como "mostrado"
		}

		if (g_lcd_dirty && g_lcd_mutex) {
			xSemaphoreTake(g_lcd_mutex, portMAX_DELAY);
			char l1[17]; char l2[17];
			memcpy(l1, g_lcd_line1, 17);
			memcpy(l2, g_lcd_line2, 17);
			g_lcd_dirty = false;
			xSemaphoreGive(g_lcd_mutex);

			lcd_clear();
			lcd_set_cursor(0,0); lcd_print_len(l1, 16);
			lcd_set_cursor(0,1); lcd_print_len(l2, 16);
		}
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

// =============================================================
// ====================   UTILIDADES BUZZER   ==================
// =============================================================

static void buzzer_init(void)
{
	ledc_timer_config_t tcfg = {
		.speed_mode = BUZZER_LEDC_MODE,
		.duty_resolution = LEDC_TIMER_10_BIT,
		.timer_num = BUZZER_LEDC_TIMER,
		.freq_hz = BUZZER_FREQ_HZ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	ledc_timer_config(&tcfg);

	ledc_channel_config_t ch = {
		.gpio_num = BUZZER_GPIO,
		.speed_mode = BUZZER_LEDC_MODE,
		.channel = BUZZER_LEDC_CHANNEL,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = BUZZER_LEDC_TIMER,
		.duty = 0,
		.hpoint = 0,
	};
	ledc_channel_config(&ch);
}

static void buzzer_play_ms(uint32_t ms, uint32_t duty)
{
	ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
	ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
	vTaskDelay(pdMS_TO_TICKS(ms));
	ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
	ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void beep_ok(void)      { buzzer_play_ms(80, 300); vTaskDelay(pdMS_TO_TICKS(40)); buzzer_play_ms(80, 300); }
static void beep_error(void)   { buzzer_play_ms(300, 500); }
static void beep_tick(void)    { buzzer_play_ms(30, 300); }

// Patrones solicitados para contraseña:
// - Pip único al capturar un dígito
// - Doble pip al acertar la contraseña (ya cubierto por beep_ok)
// - Triple pip al fallar la contraseña
static void beep_triple(void)
{
	beep_tick();
	vTaskDelay(pdMS_TO_TICKS(40));
	beep_tick();
	vTaskDelay(pdMS_TO_TICKS(40));
	beep_tick();
}

// =============================================================
// ==================   UTILIDADES DE LEDS   ===================
// =============================================================

static inline void led_set(gpio_num_t gpio, bool on)
{
	gpio_set_level(gpio, on ? 1 : 0);
}

static void leds_init(void)
{
	gpio_config_t io = {
		.pin_bit_mask = (1ULL<<LED_STATUS_GPIO) | (1ULL<<LED_GREEN_GPIO) | (1ULL<<LED_RED_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	gpio_config(&io);

	// Azul (status) siempre encendido
	led_set(LED_STATUS_GPIO, 1);
	// En reposo: verde y rojo apagados. Verde solo al conceder acceso; rojo solo al denegar.
	led_set(LED_GREEN_GPIO, 0);
	led_set(LED_RED_GPIO, 0);
}

// =============================================================
// ===================   CONTROL DE LOCK   =====================
// =============================================================

// ====== Servo driver ======
#if LOCK_USE_SERVO
static void servo_init(void)
{
	ledc_timer_config_t tcfg = {
		.speed_mode = SERVO_LEDC_MODE,
		.duty_resolution = SERVO_TIMER_RES,
		.timer_num = SERVO_LEDC_TIMER,
		.freq_hz = SERVO_FREQ_HZ,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	ledc_timer_config(&tcfg);

	ledc_channel_config_t ch = {
		.gpio_num = SERVO_GPIO,
		.speed_mode = SERVO_LEDC_MODE,
		.channel = SERVO_LEDC_CHANNEL,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = SERVO_LEDC_TIMER,
		.duty = 0,
		.hpoint = 0,
	};
	ledc_channel_config(&ch);
}

static void servo_write_pulse_us(uint32_t us)
{
	const uint32_t period_us = 1000000UL / SERVO_FREQ_HZ; // ~20000 us
	const uint32_t max_duty = (1U << SERVO_TIMER_BITS) - 1;
	if (us < SERVO_MIN_US) us = SERVO_MIN_US;
	if (us > SERVO_MAX_US) us = SERVO_MAX_US;
	uint32_t duty = (uint32_t)((((uint64_t)us) * max_duty + (period_us/2)) / period_us);
	if (duty > max_duty) duty = max_duty;
	ledc_set_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL, duty);
	ledc_update_duty(SERVO_LEDC_MODE, SERVO_LEDC_CHANNEL);
}

static inline void servo_set_locked(bool locked)
{
	// Mapea ángulos a microsegundos dentro de [SERVO_MIN_US, SERVO_MAX_US]
	int angle = locked ? SERVO_LOCK_DEG : SERVO_UNLOCK_DEG;
	if (angle < 0) angle = 0;
	if (angle > 180) angle = 180;
	uint32_t us = SERVO_MIN_US + (uint32_t)((((uint64_t)(SERVO_MAX_US - SERVO_MIN_US)) * angle) / 180);
	servo_write_pulse_us(us);
}
#endif

static void lock_hw_init(void)
{
#if LOCK_USE_SERVO
	servo_init();
#else
	gpio_config_t io = {
		.pin_bit_mask = (1ULL<<LOCK_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	gpio_config(&io);
#endif
}

#if !LOCK_USE_SERVO
static void lock_apply_level(bool lock_on)
{
	// lock_on = true significa “energizar imán” (cerrado), según LOCK_ACTIVE_HIGH
	int level = (LOCK_ACTIVE_HIGH ? (lock_on ? 1 : 0) : (lock_on ? 0 : 1));
	gpio_set_level(LOCK_GPIO, level);
}
#endif

static inline void lock_apply_locked_hw(bool locked)
{
#if LOCK_USE_SERVO
	servo_set_locked(locked);
#else
	lock_apply_level(locked);
#endif
}

static void set_locked_state(bool locked)
{
	g_lock_state = locked ? LOCKED : UNLOCKED;
	if (locked) {
		xEventGroupSetBits(g_events, EVT_LOCKED);
		// Estado bloqueado: solo LED de status (azul). Verde y rojo apagados.
		led_set(LED_GREEN_GPIO, 0);
		led_set(LED_RED_GPIO, 0);
	} else {
		xEventGroupClearBits(g_events, EVT_LOCKED);
		// Acceso concedido: verde encendido, rojo apagado.
		led_set(LED_RED_GPIO, 0);
		led_set(LED_GREEN_GPIO, 1);
	}
}

static void led_show_denied(void)
{
    // Muestra acceso denegado: rojo encendido breve y se apaga
    led_set(LED_RED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    led_set(LED_RED_GPIO, 0);
}

static void lock_door(void)
{
	if (g_door_state != DOOR_CLOSED) {
		ESP_LOGW(TAG, "Intento de lock ignorado: puerta no está cerrada");
		return;
	}
	lock_apply_locked_hw(true);
	set_locked_state(true);
	g_pending_relock = false;
	// Al volver a cerrar la cerradura, reiniciar la captura de combinación
	combo_reset();
	ESP_LOGI(TAG, "Cerradura BLOQUEADA (lock)");
	// Mostrar LOCKING... por 1 segundo y luego volver a idle (no bloqueante)
	lcd_set_message("LOCKING...", "");
	touch_activity();
	g_locking_until_us = esp_timer_get_time() + 1000000; // 1s
}

static void unlock_door(void)
{
	lock_apply_locked_hw(false);
	set_locked_state(false);
	g_pending_relock = true;  // Se re-bloqueará cuando detectemos la puerta cerrada
	ESP_LOGI(TAG, "Cerradura DESBLOQUEADA (unlock)");
}

// =============================================================
// ==============   SENSOR DE PUERTA (REED)   ==================
// =============================================================

static void door_sensor_init(void)
{
	gpio_config_t io = {
		.pin_bit_mask = (1ULL<<DOOR_SENSOR_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,   // Suponemos reed a GND cuando puerta cerrada u abierta (ajustar cableado)
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};
	gpio_config(&io);
}

static door_state_t read_door_state(void)
{
	// NOTA: Ajusta esta lógica según tu cableado. Aquí asumimos:
	//  - Reed cerrado => GPIO en 0
	//  - Reed abierto  => GPIO en 1
	int level = gpio_get_level(DOOR_SENSOR_GPIO);
	return (level == 0) ? DOOR_CLOSED : DOOR_OPEN;
}

static void door_monitor_task(void *arg)
{
	door_state_t last = DOOR_UNKNOWN;
	int64_t unlock_start_us = 0;

	for (;;) {
		door_state_t now = read_door_state();
		int64_t now_us = esp_timer_get_time();
		if (now != last) {
			last = now;
			g_door_state = now;
			if (now == DOOR_CLOSED) {
				xEventGroupSetBits(g_events, EVT_DOOR_CLOSED);
				ESP_LOGI(TAG, "Puerta: CERRADA");
				// Si hay un re-bloqueo pendiente, armar bloqueo con retardo de 1s
				if (g_pending_relock) {
					g_relock_arm_time_us = now_us + 1000000; // 1 segundo
					ESP_LOGI(TAG, "Re-bloqueo armado para 1s después del cierre");
				}
			} else {
				xEventGroupClearBits(g_events, EVT_DOOR_CLOSED);
				ESP_LOGI(TAG, "Puerta: ABIERTA");
				// Si reabre, cancelar cualquier re-bloqueo armado
				g_relock_arm_time_us = 0;
			}
		}

		// Ejecutar re-bloqueo diferido si corresponde y la puerta sigue cerrada
		if (g_pending_relock && g_door_state == DOOR_CLOSED && g_relock_arm_time_us > 0 && now_us >= g_relock_arm_time_us) {
			lock_door();
			g_relock_arm_time_us = 0;
		}

		// Gestión de tiempo máximo de desbloqueo si la puerta no se abrió
		if (g_lock_state == UNLOCKED) {
			if (unlock_start_us == 0) {
				unlock_start_us = now_us;
			}
			int64_t dt_ms = (now_us - unlock_start_us) / 1000;
			if (dt_ms >= UNLOCK_MAX_OPEN_TIME_MS) {
				// Solo bloquear si la puerta está cerrada (regla del sistema)
				if (g_door_state == DOOR_CLOSED) {
					ESP_LOGI(TAG, "Tiempo max. desbloqueo alcanzado con puerta cerrada => lock");
					lock_door();
				} else {
					// Si está abierta, esperamos a que cierre para poder lock
					ESP_LOGW(TAG, "Tiempo max. alcanzado pero puerta ABIERTA; esperando cierre para lock");
				}
				unlock_start_us = 0; // Reiniciar para el siguiente ciclo de unlock
			}
		} else {
			unlock_start_us = 0; // No estamos desbloqueados, contador detenido
		}

		vTaskDelay(pdMS_TO_TICKS(50));
	}
}

// =============================================================
// ==================   ENCODER (COMBINACIÓN)   =================
// =============================================================

// ================= POTENCIÓMETRO (LECTURA ANALÓGICA) =================
#include "esp_adc/adc_oneshot.h"
static adc_oneshot_unit_handle_t g_adc_handle;
static adc_channel_t g_adc_channel = ADC_CHANNEL_6; // GPIO34 -> ADC1_CH6

static void pot_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&unit_cfg, &g_adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11, // Mayor rango de entrada (~0-3.3V)
    };
    adc_oneshot_config_channel(g_adc_handle, g_adc_channel, &chan_cfg);
}

static int pot_raw_to_digit(int raw)
{
    if (raw < 0) raw = 0;
    if (raw > POT_ADC_MAX_RAW) raw = POT_ADC_MAX_RAW;
    // Redondeo: distribuye 0..4095 en 11 segmentos para 0..10
    // digit = round(raw / (POT_ADC_MAX_RAW / POT_MAX_DIGIT))
    // Avoid float: (raw * POT_MAX_DIGIT + POT_ADC_MAX_RAW/2) / POT_ADC_MAX_RAW
    int digit = (raw * POT_MAX_DIGIT + POT_ADC_MAX_RAW/2) / POT_ADC_MAX_RAW;
    if (digit < 0) digit = 0;
    if (digit > POT_MAX_DIGIT) digit = POT_MAX_DIGIT;
    return digit;
}

static void combo_reset(void)
{
	for (int i=0;i<COMBO_LEN;++i) g_entered[i]=0;
	g_entered_count = 0;
	g_digit_captured_after_settle = false;
	xEventGroupClearBits(g_events, EVT_COMBO_OK);
}

static bool combo_is_correct(void)
{
	for (int i=0;i<COMBO_LEN;++i) {
		if (g_entered[i] != COMBO_TARGET[i]) return false;
	}
	return true;
}

static void pot_task(void *arg)
{
	int last_digit_for_log = -1;
	bool moved_since_last_capture = false; // Requiere movimiento antes de considerar nuevo dígito
	combo_reset();
	// Mostrar mensaje idle al iniciar
	lcd_show_idle();
	touch_activity();
	for (;;) {
		int raw = 0;
		if (adc_oneshot_read(g_adc_handle, g_adc_channel, &raw) == ESP_OK) {
			int digit = pot_raw_to_digit(raw);
			int64_t now_us = esp_timer_get_time();

			if (digit != g_current_digit) {
				// Movimiento detectado
				g_current_digit = digit;
				g_last_move_ts_us = now_us;
				g_digit_captured_after_settle = false; // Permitirá capturar el nuevo valor cuando se estabilice
				moved_since_last_capture = true;
			}

			// Log del número del potenciómetro: solo al cambiar y con anti-spam (>= POT_LOG_MIN_MS)
			if (digit != last_digit_for_log && (now_us - g_last_pot_print_us) >= (int64_t)POT_LOG_MIN_MS*1000) {
				ESP_LOGI(TAG, "Potenciómetro dígito actual: %d", digit);
				g_last_pot_print_us = now_us;
				last_digit_for_log = digit;
			}

			// Estabilidad: si ha pasado POT_SETTLE_MS desde el último movimiento y aún no se capturó
			if (moved_since_last_capture && !g_digit_captured_after_settle && (now_us - g_last_move_ts_us)/1000 >= POT_SETTLE_MS) {
				// Capturamos el dígito estabilizado como parte de la combinación
				if (g_entered_count < COMBO_LEN) {
					g_entered[g_entered_count++] = g_current_digit;
					ESP_LOGI(TAG, "Dígito capturado: %d (progreso %d/%d)", g_current_digit, g_entered_count, COMBO_LEN);
					// Pip único por dígito ingresado
					beep_tick();
					// LCD: mostrar progreso de contraseña
					char l1[17] = "CURRENT PASS:";
					char l2[17];
					int d0 = (g_entered_count >= 1) ? g_entered[0] : -1;
					int d1 = (g_entered_count >= 2) ? g_entered[1] : -1;
					int d2 = (g_entered_count >= 3) ? g_entered[2] : -1;
					snprintf(l2, sizeof(l2), "%c %c %c",
						 (d0>=0? ('0'+d0): '#'),
						 (d1>=0? ('0'+d1): '#'),
						 (d2>=0? ('0'+d2): '#'));
					lcd_set_message(l1, l2);
					touch_activity();
					g_digit_captured_after_settle = true; // Evita múltiples capturas sin movimiento
					moved_since_last_capture = false;      // Requiere nuevo movimiento para el próximo dígito

					if (g_entered_count == COMBO_LEN) {
						if (combo_is_correct()) {
							ESP_LOGI(TAG, "Combinación CORRECTA (%d %d %d)", g_entered[0], g_entered[1], g_entered[2]);
							// Doble pip por contraseña correcta
							beep_ok();
							lcd_set_message("ACCESS GRANTED!", "WELCOME HOME");
							touch_activity();
							xEventGroupSetBits(g_events, EVT_COMBO_OK);
						} else {
							ESP_LOGW(TAG, "Combinación INCORRECTA (%d %d %d != %d %d %d)",
									 g_entered[0], g_entered[1], g_entered[2],
									 COMBO_TARGET[0], COMBO_TARGET[1], COMBO_TARGET[2]);
							// Triple pip por contraseña incorrecta
							beep_triple();
							led_show_denied();
							lcd_set_message("ACCESS DENIED!", "");
							touch_activity();
							combo_reset();
							moved_since_last_capture = false; // Se exigirá movimiento antes de capturar de nuevo
						}
					}
				}
			}

			// Si la combinación ya fue validada, mantener el evento hasta que se mueva de nuevo
			if ((xEventGroupGetBits(g_events) & EVT_COMBO_OK) && (digit != COMBO_TARGET[COMBO_LEN-1])) {
				// Opcional: podríamos limpiar al mover, pero lo dejamos persistir hasta uso por control_task
			}
		}
		vTaskDelay(pdMS_TO_TICKS(120));
	}
}

// =============================================================
// ===================   RFID (MFRC522)   ======================
// =============================================================

#if USE_MFRC522
#include "mfrc522_min.h"

static bool uid_is_authorized(const uint8_t *uid, size_t len)
{
	if (len < 4) return false;
	for (size_t i=0; i<AUTH_UIDS_COUNT; ++i) {
		if (memcmp(uid, AUTH_UIDS[i], 4) == 0) return true;
	}
	return false;
}

static void rfid_task(void *arg)
{
	ESP_LOGI(TAG, "RFID (MFRC522) habilitado");
	mfrc522_t rfid = {0};
	if (!mfrc522_init(&rfid, SPI3_HOST, RFID_SPI_SCK_GPIO, RFID_SPI_MOSI_GPIO, RFID_SPI_MISO_GPIO, RFID_SPI_CS_GPIO, RFID_RST_GPIO)) {
		ESP_LOGE(TAG, "Error inicializando MFRC522");
	}

	uint8_t ver = 0;
	if (mfrc522_get_version(&rfid, &ver)) {
		ESP_LOGI(TAG, "MFRC522 VersionReg=0x%02X", ver);
	}

	uint8_t last_uid[10] = {0};
	size_t last_uid_len = 0;
	bool card_present_last = false;

	for (;;) {
		uint8_t atqa[2] = {0}; size_t atqa_len = sizeof(atqa);
		bool present = mfrc522_request_a(&rfid, atqa, &atqa_len);
		if (present) {
			uint8_t uid[10] = {0};
			size_t uid_len = 0;
			// Intentamos anticollision nivel 1 (4 bytes de UID base)
			if (mfrc522_anticoll_cl1(&rfid, uid)) {
				uid_len = 4;
				bool is_new = (!card_present_last) || (uid_len != last_uid_len) || (memcmp(uid, last_uid, uid_len) != 0);
				if (is_new) {
					ESP_LOGI(TAG, "RFID UID: %02X:%02X:%02X:%02X", uid[0], uid[1], uid[2], uid[3]);
					// Pip único por escaneo
					beep_tick();
					touch_activity();
					// Autorización por lista blanca
					if (uid_is_authorized(uid, uid_len)) {
						ESP_LOGI(TAG, "RFID autorizado (whitelist)");
						lcd_set_message("ACCESS GRANTED!", "WELCOME HOME");
						touch_activity();
						xEventGroupSetBits(g_events, EVT_RFID_OK);
					} else {
						ESP_LOGW(TAG, "RFID NO autorizado");
						led_show_denied();
						lcd_set_message("ACCESS DENIED!", "");
						touch_activity();
					}
					memcpy(last_uid, uid, uid_len);
					last_uid_len = uid_len;
				}
				card_present_last = true;
			} else {
				// No se pudo leer UID en CL1, consideramos como no presente para evitar spam
				card_present_last = false;
				last_uid_len = 0;
			}
		} else {
			card_present_last = false;
			last_uid_len = 0;
		}
		vTaskDelay(pdMS_TO_TICKS(150));
	}
}

#else
static void rfid_task(void *arg)
{
	// Stub sin RFID real: solo informa. Deja el bit EVT_RFID_OK sin activar.
	ESP_LOGW(TAG, "RFID deshabilitado (USE_MFRC522=0). Ver README para habilitar.");
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
#endif

// =============================================================
// ===================   LÓGICA PRINCIPAL   ====================
// =============================================================

static void control_task(void *arg)
{
	// Arrancamos bloqueados si la puerta está cerrada
	if (read_door_state() == DOOR_CLOSED) {
		lock_door();
	} else {
		// Si por cableado la puerta arranca abierta, bloqueamos igual pero 
		// recordando que no podremos lock hasta cerrarla; por seguridad, mantenemos nivel de lock activo.
		lock_apply_locked_hw(true);
		set_locked_state(true);
	}

	for (;;) {
		EventBits_t bits = 0;
		if (ACCESS_MODE == ACCESS_MODE_AND) {
			ESP_LOGI(TAG, "Modo AND: esperando RFID y combinación...");
			bits = xEventGroupWaitBits(g_events, EVT_RFID_OK | EVT_COMBO_OK, pdTRUE, pdTRUE, portMAX_DELAY);
		} else {
			ESP_LOGI(TAG, "Modo OR: esperando RFID o combinación...");
			bits = xEventGroupWaitBits(g_events, EVT_RFID_OK | EVT_COMBO_OK, pdTRUE, pdFALSE, portMAX_DELAY);
		}

		// Si llegó aquí, se ha cumplido la condición de acceso
		(void)bits;
		// Desbloquear solo si la puerta está CERRADA; si no, esperar a que se cierre
		if (g_door_state != DOOR_CLOSED) {
			ESP_LOGW(TAG, "Acceso listo pero puerta ABIERTA; esperando cierre para desbloquear");
			xEventGroupWaitBits(g_events, EVT_DOOR_CLOSED, pdFALSE, pdTRUE, portMAX_DELAY);
		}
		unlock_door();

		// Tras conceder acceso, limpiamos cualquier otro “OK” pendiente para el siguiente ciclo.
		xEventGroupClearBits(g_events, EVT_RFID_OK | EVT_COMBO_OK);
	}
}

// =============================================================
// ==================   INICIALIZACIÓN GENERAL   ===============
// =============================================================

static void gpio_basic_init(void)
{
	// Nada especial aquí por ahora
}

void app_main(void)
{
	ESP_LOGI(TAG, "Sistema de Acceso y Monitoreo de Seguridad");

	g_events = xEventGroupCreate();

	gpio_basic_init();
	leds_init();
	buzzer_init();
#if LCD_AUTOPROBE
	// Ejecuta auto-probe visual antes de usar el driver LCD normal
	lcd_autoprobe_run();
#endif
	door_sensor_init();
	lock_hw_init();
	pot_init();
	lcd_init();
#if LCD_DEBUG_PATTERN
	lcd_debug_pattern(); // Muestra patrón inicial para validar caracteres antes de flujo normal
#endif
	lcd_show_idle();
	touch_activity();

	// Estado inicial de puerta
	g_door_state = read_door_state();
	if (g_door_state == DOOR_CLOSED) {
		xEventGroupSetBits(g_events, EVT_DOOR_CLOSED);
	} else {
		xEventGroupClearBits(g_events, EVT_DOOR_CLOSED);
	}

	// Tareas
	xTaskCreatePinnedToCore(door_monitor_task, "door_mon", 4096, NULL, 6, NULL, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(pot_task,         "pot",      4096, NULL, 5, NULL, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(rfid_task,         "rfid",     4096, NULL, 4, NULL, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(control_task,      "control",  4096, NULL, 7, NULL, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(lcd_task,          "lcd",      3072, NULL, 3, NULL, tskNO_AFFINITY);
}

