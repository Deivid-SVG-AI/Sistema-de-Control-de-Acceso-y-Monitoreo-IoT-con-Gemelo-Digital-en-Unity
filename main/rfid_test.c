#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

#include "mfrc522_min.h"

static const char *TAG = "RFID_TEST";

// --- Buzzer config (matches project defaults) ---
#define BUZZER_GPIO               GPIO_NUM_26
#define BUZZER_LEDC_TIMER         LEDC_TIMER_0
#define BUZZER_LEDC_MODE          LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_CHANNEL       LEDC_CHANNEL_0
#define BUZZER_FREQ_HZ            2000

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

static void beep_tick(void) { buzzer_play_ms(30, 300); }

// --- MFRC522 pins (match project defaults) ---
#define RFID_SPI_CS_GPIO          GPIO_NUM_5
#define RFID_SPI_SCK_GPIO         GPIO_NUM_18
#define RFID_SPI_MOSI_GPIO        GPIO_NUM_23
#define RFID_SPI_MISO_GPIO        GPIO_NUM_19
#define RFID_RST_GPIO             GPIO_NUM_13

void app_main(void)
{
    ESP_LOGI(TAG, "RFID test: print UID and beep on scan");

    buzzer_init();

    mfrc522_t rfid = {0};
    if (!mfrc522_init(&rfid, SPI3_HOST, RFID_SPI_SCK_GPIO, RFID_SPI_MOSI_GPIO, RFID_SPI_MISO_GPIO, RFID_SPI_CS_GPIO, RFID_RST_GPIO)) {
        ESP_LOGE(TAG, "Failed to init MFRC522");
    }

    uint8_t ver = 0;
    if (mfrc522_get_version(&rfid, &ver)) {
        ESP_LOGI(TAG, "MFRC522 VersionReg=0x%02X", ver);
        if (ver == 0x00 || ver == 0xFF) {
            ESP_LOGW(TAG, "Version inválida (0x%02X). Revisa cableado de SPI/CS/RST y alimentación 3.3V.", ver);
        }
    } else {
        ESP_LOGW(TAG, "No se pudo leer VersionReg. Revisa conexiones y alimentación.");
    }

    uint8_t last_uid[10] = {0};
    size_t last_uid_len = 0;
    bool card_present_last = false;

    while (true) {
        uint8_t atqa[2] = {0}; size_t atqa_len = sizeof(atqa);
        bool present = mfrc522_request_a(&rfid, atqa, &atqa_len);
        if (present) {
            uint8_t uid[10] = {0};
            size_t uid_len = 0;
            if (mfrc522_anticoll_cl1(&rfid, uid)) {
                uid_len = 4;
                bool is_new = (!card_present_last) || (uid_len != last_uid_len) || (memcmp(uid, last_uid, uid_len) != 0);
                if (is_new) {
                    ESP_LOGI(TAG, "RFID UID: %02X:%02X:%02X:%02X", uid[0], uid[1], uid[2], uid[3]);
                    beep_tick();
                    memcpy(last_uid, uid, uid_len);
                    last_uid_len = uid_len;
                }
                card_present_last = true;
            } else {
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
