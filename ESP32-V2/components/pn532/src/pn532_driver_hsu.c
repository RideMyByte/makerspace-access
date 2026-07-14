#include <string.h>
#include "pn532_driver.h"
#include "pn532_driver_hsu.h"
#include "esp_log.h"

static const char TAG[] = "pn532_hsu";

typedef struct {
    gpio_num_t uart_rx;
    gpio_num_t uart_tx;
    uart_port_t uart_port;
    uint8_t uart_baud_wanted;
    uint8_t uart_baud_used;
} pn532_hsu_driver_config;

const int32_t baud_table[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1288000};

static const uint8_t fw_cmd[] = {0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A};
static const uint8_t baud_resp[] = {0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD5, 0x11, 0x1A, 0x00};

static esp_err_t pn532_init_io(pn532_io_handle_t);
static void pn532_release_io(pn532_io_handle_t);
static void pn532_release_driver(pn532_io_handle_t);
static esp_err_t pn532_read(pn532_io_handle_t, uint8_t*, size_t, int);
static esp_err_t pn532_write(pn532_io_handle_t, const uint8_t*, size_t, int);
static esp_err_t pn532_wakeup(pn532_hsu_driver_config*);
static esp_err_t pn532_init_extra(pn532_io_handle_t);

esp_err_t pn532_new_driver_hsu(gpio_num_t uart_rx, gpio_num_t uart_tx, gpio_num_t reset, gpio_num_t irq,
                               uart_port_t uart_port, int32_t baudrate, pn532_io_handle_t io_handle) {
    if (!io_handle || uart_rx == GPIO_NUM_NC || uart_tx == GPIO_NUM_NC || uart_port >= UART_NUM_MAX)
        return ESP_ERR_INVALID_ARG;

    pn532_hsu_driver_config *cfg = calloc(1, sizeof(pn532_hsu_driver_config));
    if (!cfg) return ESP_ERR_NO_MEM;

    io_handle->reset = reset;
    io_handle->irq = irq;
    cfg->uart_port = uart_port;
    cfg->uart_rx = uart_rx;
    cfg->uart_tx = uart_tx;
    cfg->uart_baud_wanted = 4; // 115200
    for (int n = 0; n < 9; n++) {
        if (baud_table[n] == baudrate) { cfg->uart_baud_wanted = n; break; }
    }
    io_handle->driver_data = cfg;

    io_handle->pn532_init_io = pn532_init_io;
    io_handle->pn532_release_driver = pn532_release_driver;
    io_handle->pn532_release_io = pn532_release_io;
    io_handle->pn532_read = pn532_read;
    io_handle->pn532_write = pn532_write;
    io_handle->pn532_init_extra = pn532_init_extra;
    io_handle->pn532_is_ready = NULL;
    return ESP_OK;
}

void pn532_release_driver(pn532_io_handle_t io) {
    if (!io || !io->driver_data) return;
    pn532_release_io(io);
    free(io->driver_data);
    io->driver_data = NULL;
}

esp_err_t pn532_init_io(pn532_io_handle_t io) {
    if (!io || !io->driver_data) return ESP_ERR_INVALID_ARG;
    pn532_hsu_driver_config *cfg = io->driver_data;

    cfg->uart_baud_used = 4;
    uart_config_t uart = {
        .baud_rate = baud_table[cfg->uart_baud_used],
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_driver_install(cfg->uart_port, 256, 256, 0, NULL, 0) != ESP_OK) return ESP_FAIL;
    if (uart_param_config(cfg->uart_port, &uart) != ESP_OK) return ESP_FAIL;
    if (uart_set_pin(cfg->uart_port, cfg->uart_tx, cfg->uart_rx, GPIO_NUM_NC, GPIO_NUM_NC) != ESP_OK) return ESP_FAIL;

    // Try default baud
    pn532_write(io, fw_cmd, sizeof(fw_cmd), 100);
    uint8_t buf[8];
    esp_err_t err = pn532_read(io, buf, 6, 100);
    if (err == ESP_OK && buf[3] == 0x00 && buf[4] == 0xFF) return ESP_OK;

    // Try configured baud
    if (cfg->uart_baud_wanted != cfg->uart_baud_used) {
        uart_set_baudrate(cfg->uart_port, baud_table[cfg->uart_baud_wanted]);
        cfg->uart_baud_used = cfg->uart_baud_wanted;
        pn532_write(io, fw_cmd, sizeof(fw_cmd), 100);
        err = pn532_read(io, buf, 6, 100);
        if (err == ESP_OK && buf[3] == 0x00 && buf[4] == 0xFF) return ESP_OK;
    }
    return ESP_FAIL;
}

void pn532_release_io(pn532_io_handle_t io) {
    if (!io || !io->driver_data) return;
    pn532_hsu_driver_config *cfg = io->driver_data;
    if (uart_is_driver_installed(cfg->uart_port)) uart_driver_delete(cfg->uart_port);
}

esp_err_t pn532_init_extra(pn532_io_handle_t io) {
    if (!io || !io->driver_data) return ESP_ERR_INVALID_ARG;
    pn532_hsu_driver_config *cfg = io->driver_data;
    if (cfg->uart_baud_used == cfg->uart_baud_wanted) return ESP_OK;

    // SetSerialBaudRate command
    uint8_t buf[16] = {
        0x00, 0xFF, 0x03, 0xFD, 0xD4, 0x10, cfg->uart_baud_wanted, 0
    };
    buf[7] = ~(buf[4] + buf[5] + buf[6]) + 1;

    pn532_write(io, buf, 8, 100);
    pn532_read(io, buf, 6, 100); // ACK
    if (memcmp(buf, (uint8_t[]){0x00,0x00,0xFF,0x00,0xFF,0x00}, 6) != 0) return ESP_FAIL;

    pn532_read(io, buf, sizeof(baud_resp), 100); // response
    if (memcmp(buf, baud_resp, sizeof(baud_resp)) != 0) return ESP_FAIL;

    pn532_write(io, (uint8_t[]){0x00,0xFF,0x00,0xFF,0x00}, 5, 100); // ACK back
    vTaskDelay(pdMS_TO_TICKS(10));
    uart_set_baudrate(cfg->uart_port, baud_table[cfg->uart_baud_wanted]);
    cfg->uart_baud_used = cfg->uart_baud_wanted;
    return ESP_OK;
}

esp_err_t pn532_wakeup(pn532_hsu_driver_config *cfg) {
    uint8_t wake[] = {0x55, 0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    if (uart_write_bytes(cfg->uart_port, wake, sizeof(wake)) != sizeof(wake)) return ESP_FAIL;
    return uart_wait_tx_done(cfg->uart_port, pdMS_TO_TICKS(100));
}

esp_err_t pn532_read(pn532_io_handle_t io, uint8_t *buf, size_t size, int timeout_ms) {
    if (!io || !io->driver_data || !buf || size < 6) return ESP_ERR_INVALID_ARG;
    pn532_hsu_driver_config *cfg = io->driver_data;

    int rx = uart_read_bytes(cfg->uart_port, buf, 6, timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) + 1 : portMAX_DELAY);
    if (rx != 6) return (rx < 0) ? ESP_FAIL : ESP_ERR_TIMEOUT;

    if (memcmp(buf, (uint8_t[]){0x00,0x00,0xFF,0x00,0xFF,0x00}, 6) == 0) return ESP_OK; // ACK
    if (memcmp(buf, (uint8_t[]){0x00,0x00,0xFF,0xFF,0x00,0x00}, 6) == 0) return ESP_OK; // NACK

    uint8_t len = buf[3];
    if ((len + buf[4]) & 0xFF) return ESP_FAIL;

    if (len > size - 6) len = size - 6;
    rx = uart_read_bytes(cfg->uart_port, buf + 6, len + 1, pdMS_TO_TICKS(timeout_ms));
    if (rx != len + 1) return ESP_ERR_TIMEOUT;

    uint8_t csum = 0;
    for (int i = 0; i < len; i++) csum += buf[5 + i];
    csum += buf[5 + len]; // DCS
    return (csum == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t pn532_write(pn532_io_handle_t io, const uint8_t *data, size_t len, int timeout_ms) {
    if (!io || !io->driver_data) return ESP_ERR_INVALID_ARG;
    pn532_hsu_driver_config *cfg = io->driver_data;

    if (!io->isSAMConfigDone) {
        esp_err_t e = pn532_wakeup(cfg);
        if (e != ESP_OK) return e;
    }
    uart_flush_input(cfg->uart_port);

    uint8_t pre = 0;
    if (uart_write_bytes(cfg->uart_port, &pre, 1) != 1) return ESP_FAIL;
    if (uart_write_bytes(cfg->uart_port, data, len) != (int)len) return ESP_FAIL;
    if (uart_write_bytes(cfg->uart_port, &pre, 1) != 1) return ESP_FAIL;
    return uart_wait_tx_done(cfg->uart_port, pdMS_TO_TICKS(timeout_ms));
}