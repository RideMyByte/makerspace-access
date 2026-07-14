// PN532 high-level functions (from garag/esp-idf-pn532, MIT license)
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "pn532.h"

static const char TAG[] = "PN532";
const uint8_t pn532response_firmwarevers[] = {0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03};
static uint8_t pn532_inListedTag;
#define PN532_COMMAND_BUFFER_LEN 64
uint8_t pn532_packetbuffer[PN532_COMMAND_BUFFER_LEN];

esp_err_t pn532_get_firmware_version(pn532_io_handle_t io_handle, uint32_t *fw_version) {
    pn532_packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;
    esp_err_t err = pn532_send_command_wait_ack(io_handle, pn532_packetbuffer, 1, PN532_WRITE_TIMEOUT);
    if (err != ESP_OK) return err;
    err = pn532_wait_ready(io_handle, 100);
    if (err != ESP_OK) return err;
    err = pn532_read_data(io_handle, pn532_packetbuffer, 12, PN532_READ_TIMEOUT);
    if (err != ESP_OK) return err;
    if (0 != memcmp(pn532_packetbuffer + 1, pn532response_firmwarevers, sizeof(pn532response_firmwarevers))) return ESP_FAIL;
    *fw_version = (pn532_packetbuffer[7] << 24) | (pn532_packetbuffer[8] << 16) | (pn532_packetbuffer[9] << 8) | pn532_packetbuffer[10];
    return ESP_OK;
}

esp_err_t pn532_set_passive_activation_retries(pn532_io_handle_t io_handle, uint8_t maxRetries) {
    pn532_packetbuffer[0] = PN532_COMMAND_RFCONFIGURATION;
    pn532_packetbuffer[1] = 5;
    pn532_packetbuffer[2] = 0xFF;
    pn532_packetbuffer[3] = 0x01;
    pn532_packetbuffer[4] = maxRetries;
    return pn532_send_command_wait_ack(io_handle, pn532_packetbuffer, 5, PN532_WRITE_TIMEOUT);
}

esp_err_t pn532_read_passive_target_id(pn532_io_handle_t io_handle, uint8_t baud, uint8_t *uid, uint8_t *uid_len, int32_t timeout) {
    pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 1;
    pn532_packetbuffer[2] = baud;
    esp_err_t err = pn532_send_command_wait_ack(io_handle, pn532_packetbuffer, 3, PN532_WRITE_TIMEOUT);
    if (err != ESP_OK) return err;
    err = pn532_wait_ready(io_handle, timeout);
    if (err != ESP_OK) return err;
    err = pn532_read_data(io_handle, pn532_packetbuffer, 32, timeout);
    if (err != ESP_OK) return err;
    if (pn532_packetbuffer[7] != 1) return ESP_FAIL;
    *uid_len = pn532_packetbuffer[12];
    for (uint8_t i = 0; i < *uid_len; i++) uid[i] = pn532_packetbuffer[13 + i];
    return ESP_OK;
}