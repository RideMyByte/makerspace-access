// PN532 SPI driver for ESP32-C6
// Based on garag/esp-idf-pn532 (MIT license)
// Adapted for makerspace-access project

#include "pn532_driver.h"
#include "pn532_driver_spi.h"

#include <string.h>
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char TAG[] = "pn532_spi";

#define OP_READ_STATUS 0x02
#define OP_WRITE_DATA 0x01
#define OP_READ_DATA 0x03

typedef struct {
    gpio_num_t miso;
    gpio_num_t mosi;
    gpio_num_t sck;
    gpio_num_t cs;
    spi_host_device_t spi_host;
    spi_device_handle_t spi_handle;
    int32_t clock_frequency;
    bool bus_initialized;
    uint8_t frame_buffer[256];
} pn532_spi_driver_config;

static esp_err_t pn532_init_io(pn532_io_handle_t io_handle);
static void pn532_release_driver(pn532_io_handle_t io_handle);
static void pn532_release_io(pn532_io_handle_t io_handle);
static esp_err_t pn532_read(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms);
static esp_err_t pn532_write(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms);
static esp_err_t pn532_is_ready(pn532_io_handle_t io_handle);

esp_err_t pn532_new_driver_spi(gpio_num_t miso,
                               gpio_num_t mosi,
                               gpio_num_t sck,
                               gpio_num_t cs,
                               gpio_num_t reset,
                               gpio_num_t irq,
                               spi_host_device_t spi_host,
                               int32_t clock_frequency,
                               pn532_io_handle_t io_handle)
{
    if (io_handle == NULL) return ESP_ERR_INVALID_ARG;
    if (spi_host == SPI_HOST_MAX) return ESP_ERR_INVALID_ARG;

    pn532_spi_driver_config *dev_config = heap_caps_calloc(1, sizeof(pn532_spi_driver_config), MALLOC_CAP_DEFAULT);
    if (dev_config == NULL) return ESP_ERR_NO_MEM;

    io_handle->reset = reset;
    io_handle->irq = irq;

    dev_config->spi_host = spi_host;
    dev_config->clock_frequency = clock_frequency ? clock_frequency : 4000000;
    dev_config->miso = miso;
    dev_config->mosi = mosi;
    dev_config->sck = sck;
    dev_config->cs = cs;
    dev_config->bus_initialized = false;
    dev_config->spi_handle = NULL;
    io_handle->driver_data = dev_config;

    io_handle->pn532_init_io = pn532_init_io;
    io_handle->pn532_release_io = pn532_release_io;
    io_handle->pn532_release_driver = pn532_release_driver;
    io_handle->pn532_read = pn532_read;
    io_handle->pn532_write = pn532_write;
    io_handle->pn532_init_extra = NULL;
    io_handle->pn532_is_ready = pn532_is_ready;

    return ESP_OK;
}

void pn532_release_driver(pn532_io_handle_t io_handle) {
    if (io_handle == NULL || io_handle->driver_data == NULL) return;
    pn532_release_io(io_handle);
    free(io_handle->driver_data);
    io_handle->driver_data = NULL;
}

void spi_pre_cb(spi_transaction_t *trans) {
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)trans->user;
    gpio_set_level(cfg->cs, 0);
    esp_rom_delay_us(100);
}

void spi_post_cb(spi_transaction_t *trans) {
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)trans->user;
    gpio_set_level(cfg->cs, 1);
}

esp_err_t pn532_init_io(pn532_io_handle_t io_handle) {
    if (io_handle == NULL || io_handle->driver_data == NULL) return ESP_ERR_INVALID_ARG;
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)io_handle->driver_data;

    if (cfg->spi_handle != NULL || cfg->bus_initialized) pn532_release_io(io_handle);
    if (cfg->cs == GPIO_NUM_NC) return ESP_ERR_INVALID_ARG;

    esp_err_t err;

    if (cfg->sck != GPIO_NUM_NC && cfg->miso != GPIO_NUM_NC && cfg->mosi != GPIO_NUM_NC) {
        spi_bus_config_t bus_config = {
            .miso_io_num = cfg->miso,
            .mosi_io_num = cfg->mosi,
            .sclk_io_num = cfg->sck,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
        };
        err = spi_bus_initialize(cfg->spi_host, &bus_config, SPI_DMA_CH_AUTO);
        if (err == ESP_OK) {
            cfg->bus_initialized = true;
        } else if (err == ESP_ERR_INVALID_STATE) {
            // Bus already initialized (by LCD) – that's fine
        } else {
            return err;
        }
        // ESP_ERR_INVALID_STATE = bus already initialized (by LCD), that's OK
    }

    // CS pin as GPIO output
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << cfg->cs,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    gpio_set_level(cfg->cs, 1);

    spi_device_interface_config_t dev_config = {
        .address_bits = 0,
        .command_bits = 8,
        .dummy_bits = 0,
        .mode = 0,
        .clock_speed_hz = (int)cfg->clock_frequency,
        .spics_io_num = -1,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_BIT_LSBFIRST,
        .queue_size = 1,
        .pre_cb = spi_pre_cb,
        .post_cb = spi_post_cb,
    };
    err = spi_bus_add_device(cfg->spi_host, &dev_config, &cfg->spi_handle);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

void pn532_release_io(pn532_io_handle_t io_handle) {
    if (io_handle == NULL || io_handle->driver_data == NULL) return;
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)io_handle->driver_data;
    gpio_set_level(cfg->cs, 1);
    if (cfg->spi_handle) { spi_bus_remove_device(cfg->spi_handle); cfg->spi_handle = NULL; }
    if (cfg->bus_initialized) { cfg->bus_initialized = false; spi_bus_free(cfg->spi_host); }
}

esp_err_t pn532_is_ready(pn532_io_handle_t io_handle) {
    uint8_t status;
    if (io_handle == NULL || io_handle->driver_data == NULL) return ESP_ERR_INVALID_ARG;
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)io_handle->driver_data;
    esp_err_t result = spi_device_polling_transmit(cfg->spi_handle,
        &(spi_transaction_t) {
            .cmd = OP_READ_STATUS,
            .rxlength = 8,
            .rx_buffer = &status,
            .user = io_handle->driver_data,
        });
    if (result != ESP_OK) return result;
    return (status & 0x01) ? ESP_OK : ESP_FAIL;
}

esp_err_t pn532_read(pn532_io_handle_t io_handle, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms) {
    uint8_t rx_buffer[1];
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = (xfer_timeout_ms > 0) ? pdMS_TO_TICKS(xfer_timeout_ms) : portMAX_DELAY;

    if (io_handle == NULL || io_handle->driver_data == NULL) return ESP_ERR_INVALID_ARG;
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)io_handle->driver_data;

    esp_err_t result = ESP_FAIL;
    bool ready = false;
    while (!ready && (xTaskGetTickCount() - start) < timeout) {
        result = spi_device_polling_transmit(cfg->spi_handle,
            &(spi_transaction_t){.cmd = OP_READ_STATUS, .rxlength = 8, .rx_buffer = rx_buffer, .user = io_handle->driver_data});
        if (result == ESP_OK && (rx_buffer[0] & 0x01)) ready = true;
    }
    if (!ready) return ESP_ERR_TIMEOUT;

    return spi_device_polling_transmit(cfg->spi_handle,
        &(spi_transaction_t){.cmd = OP_READ_DATA, .rxlength = read_size * 8, .rx_buffer = read_buffer, .user = io_handle->driver_data});
}

esp_err_t pn532_write(pn532_io_handle_t io_handle, const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms) {
    if (io_handle == NULL || io_handle->driver_data == NULL) return ESP_ERR_INVALID_ARG;
    if (write_size > 254) return ESP_ERR_INVALID_SIZE;
    pn532_spi_driver_config *cfg = (pn532_spi_driver_config *)io_handle->driver_data;

    cfg->frame_buffer[0] = 0;
    memcpy(cfg->frame_buffer + 1, write_buffer, write_size);
    cfg->frame_buffer[write_size + 1] = 0;

    return spi_device_polling_transmit(cfg->spi_handle,
        &(spi_transaction_t){.cmd = OP_WRITE_DATA, .length = (write_size + 2) * 8, .tx_buffer = cfg->frame_buffer, .user = io_handle->driver_data});
}