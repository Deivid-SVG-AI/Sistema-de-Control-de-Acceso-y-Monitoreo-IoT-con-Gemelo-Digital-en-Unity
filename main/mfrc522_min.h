#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t rst_gpio;
} mfrc522_t;

bool mfrc522_init(mfrc522_t *dev, spi_host_device_t host, gpio_num_t sck, gpio_num_t mosi, gpio_num_t miso, gpio_num_t cs, gpio_num_t rst);
bool mfrc522_get_version(mfrc522_t *dev, uint8_t *ver);
bool mfrc522_antenna_on(mfrc522_t *dev);
bool mfrc522_request_a(mfrc522_t *dev, uint8_t *atqa, size_t *atqa_len);
bool mfrc522_anticoll_cl1(mfrc522_t *dev, uint8_t *uid4);

#ifdef __cplusplus
}
#endif
