#ifndef PN532_DRIVER_H
#define PN532_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pn532_io_t;
typedef struct pn532_io_t *pn532_io_handle_t;

#define PN532_PREAMBLE   0x00
#define PN532_STARTCODE1 0x00
#define PN532_STARTCODE2 0xFF
#define PN532_POSTAMBLE  0x00
#define PN532_HOST_TO_PN532 0xD4
#define PN532_PN532TOHOST   0xD5
#define PN532_WRITE_TIMEOUT 100
#define PN532_READ_TIMEOUT  100

struct pn532_io_t {
    esp_err_t (*pn532_init_io)(pn532_io_handle_t);
    void (*pn532_release_io)(pn532_io_handle_t);
    void (*pn532_release_driver)(pn532_io_handle_t);
    esp_err_t (*pn532_read)(pn532_io_handle_t, uint8_t*, size_t, int);
    esp_err_t (*pn532_write)(pn532_io_handle_t, const uint8_t*, size_t, int);
    esp_err_t (*pn532_init_extra)(pn532_io_handle_t);
    esp_err_t (*pn532_is_ready)(pn532_io_handle_t);
    gpio_num_t reset;
    gpio_num_t irq;
    bool isSAMConfigDone;
    void *driver_data;
};

esp_err_t pn532_init(pn532_io_handle_t);
void pn532_release(pn532_io_handle_t);
void pn532_delete_driver(pn532_io_handle_t);
void pn532_reset(pn532_io_handle_t);
esp_err_t pn532_write_command(pn532_io_handle_t, const uint8_t*, uint8_t, int);
esp_err_t pn532_read_data(pn532_io_handle_t, uint8_t*, uint8_t, int32_t);
esp_err_t pn532_wait_ready(pn532_io_handle_t, int32_t);
esp_err_t pn532_SAM_config(pn532_io_handle_t);
esp_err_t pn532_send_command_wait_ack(pn532_io_handle_t, const uint8_t*, uint8_t, int32_t);
esp_err_t pn532_read_ack(pn532_io_handle_t);

#ifdef __cplusplus
}
#endif
#endif