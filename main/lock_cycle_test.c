#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Reutiliza los defines de tu proyecto principal si deseas; aquí los declaramos mínimos.
#define LOCK_GPIO GPIO_NUM_25      // Ajusta si tu hardware usa otro pin
#define LOCK_ACTIVE_HIGH 1         // 1: nivel alto energiza el electroimán
#define TAG "LOCK_TEST"

static void lock_hw_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LOCK_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io);
}

static inline void lock_apply(bool on) {
    int level = (LOCK_ACTIVE_HIGH ? (on ? 1 : 0) : (on ? 0 : 1));
    gpio_set_level(LOCK_GPIO, level);
}

static void lock_cycle_task(void *arg) {
    bool locked = true; // Comenzamos energizando (bloqueado)
    for (;;) {
        locked = !locked; // alterna estado
        lock_apply(locked);
        ESP_LOGI(TAG, "Electroimán %s", locked ? "ACTIVADO (lock)" : "DESACTIVADO (unlock)");
        vTaskDelay(pdMS_TO_TICKS(2000)); // 2 segundos
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Inicio prueba de ciclo de electroimán cada 2s");
    lock_hw_init();
    // Estado inicial
    lock_apply(true);
    xTaskCreate(lock_cycle_task, "lock_cycle", 2048, NULL, 5, NULL);
}
