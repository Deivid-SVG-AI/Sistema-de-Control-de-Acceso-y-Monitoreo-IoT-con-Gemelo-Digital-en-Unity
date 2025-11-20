#include "mfrc522_min.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MFRC522"

// MFRC522 Registers
#define CommandReg      0x01
#define ComIEnReg       0x02
#define DivIrqReg       0x05
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define ControlReg      0x0C
#define BitFramingReg   0x0D
#define CollReg         0x0E
#define ModeReg         0x11
#define TxModeReg       0x12
#define RxModeReg       0x13
#define TxControlReg    0x14
#define TxASKReg        0x15
#define RFCfgReg        0x26
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegH     0x2C
#define TReloadRegL     0x2D
#define VersionReg      0x37

// Commands
#define PCD_Idle        0x00
#define PCD_CalcCRC     0x03
#define PCD_Transceive  0x0C
#define PCD_SoftReset   0x0F

// PICC commands
#define PICC_REQA       0x26
#define PICC_SEL_CL1    0x93
#define PICC_ANTICOLL   0x20

static inline uint8_t _cmd_addr(uint8_t reg, bool read)
{
    // addr: 0xxxxxx0 with bit7=read flag
    uint8_t addr = (reg << 1) & 0x7E;
    if (read) addr |= 0x80;
    return addr;
}

static bool _spi_write(mfrc522_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { _cmd_addr(reg, false), val };
    spi_transaction_t t = { 0 };
    t.length = 16; // bits
    t.tx_buffer = tx;
    return spi_device_transmit(dev->spi, &t) == ESP_OK;
}

static bool _spi_read(mfrc522_t *dev, uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { _cmd_addr(reg, true), 0x00 };
    uint8_t rx[2] = { 0 };
    spi_transaction_t t = { 0 };
    t.length = 16;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    if (spi_device_transmit(dev->spi, &t) != ESP_OK) return false;
    *val = rx[1];
    return true;
}

static bool _set_bits(mfrc522_t *dev, uint8_t reg, uint8_t mask)
{
    uint8_t v; if (!_spi_read(dev, reg, &v)) return false; v |= mask; return _spi_write(dev, reg, v);
}
static bool _clr_bits(mfrc522_t *dev, uint8_t reg, uint8_t mask)
{
    uint8_t v; if (!_spi_read(dev, reg, &v)) return false; v &= (uint8_t)~mask; return _spi_write(dev, reg, v);
}

static void _soft_reset(mfrc522_t *dev)
{
    _spi_write(dev, CommandReg, PCD_SoftReset);
    vTaskDelay(pdMS_TO_TICKS(50));
}

bool mfrc522_get_version(mfrc522_t *dev, uint8_t *ver)
{
    return _spi_read(dev, VersionReg, ver);
}

bool mfrc522_antenna_on(mfrc522_t *dev)
{
    uint8_t val;
    if (!_spi_read(dev, TxControlReg, &val)) return false;
    if ((val & 0x03) != 0x03) {
        if (!_spi_write(dev, TxControlReg, val | 0x03)) return false;
    }
    return true;
}

bool mfrc522_init(mfrc522_t *dev, spi_host_device_t host, gpio_num_t sck, gpio_num_t mosi, gpio_num_t miso, gpio_num_t cs, gpio_num_t rst)
{
    memset(dev, 0, sizeof(*dev));
    dev->rst_gpio = rst;

    // Reset pin
    if (rst != GPIO_NUM_NC) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << rst),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        // Pulso de reset: bajo -> alto para asegurar estado conocido
        gpio_set_level(rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(rst, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    spi_bus_config_t buscfg = {
        .sclk_io_num = sck,
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };
    esp_err_t err = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", err);
        return false;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000, // 1 MHz (más robusto con cables largos)
        .mode = 0,
        .spics_io_num = cs,
        .queue_size = 3,
        .flags = 0,
    };
    if (spi_bus_add_device(host, &devcfg, &dev->spi) != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed");
        return false;
    }

    _soft_reset(dev);

    // Recommended init per datasheet/appnotes
    _spi_write(dev, TModeReg, 0x8D);
    _spi_write(dev, TPrescalerReg, 0x3E);
    _spi_write(dev, TReloadRegH, 0x00);
    _spi_write(dev, TReloadRegL, 0x1E);
    _spi_write(dev, TxASKReg, 0x40); // force 100% ASK
    _spi_write(dev, RFCfgReg, 0x70); // max RX gain
    _spi_write(dev, ModeReg, 0x3D);  // CRC preset 0x6363

    if (!mfrc522_antenna_on(dev)) return false;

    uint8_t ver = 0;
    if (mfrc522_get_version(dev, &ver)) {
        ESP_LOGI(TAG, "Version: 0x%02X", ver);
    }
    return true;
}

static bool _transceive(mfrc522_t *dev, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t *rx_len, uint8_t bit_framing, uint32_t timeout_ms)
{
    // Stop command
    _spi_write(dev, CommandReg, PCD_Idle);
    // Clear interrupts
    _spi_write(dev, ComIrqReg, 0x7F);
    // Flush FIFO
    _set_bits(dev, FIFOLevelReg, 0x80);
    // Bit framing
    _spi_write(dev, BitFramingReg, bit_framing);

    // Write FIFO
    for (size_t i = 0; i < tx_len; ++i) {
        _spi_write(dev, FIFODataReg, tx[i]);
    }

    // Start transceive
    _spi_write(dev, CommandReg, PCD_Transceive);
    _set_bits(dev, BitFramingReg, 0x80); // StartSend=1

    uint32_t start = (uint32_t)xTaskGetTickCount();
    while (true) {
        uint8_t irq = 0;
        _spi_read(dev, ComIrqReg, &irq);
        if (irq & 0x30) { // RxIRq or IdleIRq
            break;
        }
        if (((uint32_t)xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeout_ms) {
            return false;
        }
        vTaskDelay(1);
    }

    // Clear StartSend
    _clr_bits(dev, BitFramingReg, 0x80);

    // Check for errors
    uint8_t err = 0; _spi_read(dev, ErrorReg, &err);
    if (err & 0x13) { // BufferOvfl | ParityErr | ProtocolErr
        return false;
    }

    // Read FIFO level
    uint8_t level = 0; _spi_read(dev, FIFOLevelReg, &level);
    if (level == 0) return false;
    if (rx && rx_len) {
        size_t n = (*rx_len < level) ? *rx_len : level;
        for (size_t i = 0; i < n; ++i) {
            _spi_read(dev, FIFODataReg, &rx[i]);
        }
        *rx_len = n;
    }
    return true;
}

bool mfrc522_request_a(mfrc522_t *dev, uint8_t *atqa, size_t *atqa_len)
{
    uint8_t cmd = PICC_REQA;
    size_t rx_len = (atqa_len && *atqa_len) ? *atqa_len : 2;
    if (!atqa || !atqa_len) return false;
    *atqa_len = rx_len;
    // Clear collisions
    _spi_write(dev, CollReg, 0x80);
    return _transceive(dev, &cmd, 1, atqa, &rx_len, 0x07, 50);
}

bool mfrc522_anticoll_cl1(mfrc522_t *dev, uint8_t *uid4)
{
    // Send ANTICOLL: 0x93 0x20, expect 5 bytes: 4 UID + BCC
    uint8_t tx[2] = { PICC_SEL_CL1, PICC_ANTICOLL };
    uint8_t rx[5] = {0}; size_t rx_len = sizeof(rx);
    // Clear collisions and set bit framing to 0 for anticollision
    _spi_write(dev, CollReg, 0x80);
    if (!_transceive(dev, tx, 2, rx, &rx_len, 0x00, 50)) return false;
    if (rx_len < 5) return false;
    // Copy first 4 bytes UID
    memcpy(uid4, rx, 4);
    return true;
}
