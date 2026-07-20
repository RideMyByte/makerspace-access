#ifndef PN532_H
#define PN532_H

#include "pn532_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PN532_COMMAND_GETFIRMWAREVERSION    0x02
#define PN532_COMMAND_RFCONFIGURATION       0x32
#define PN532_COMMAND_INLISTPASSIVETARGET   0x4A
#define PN532_BRTY_ISO14443A_106KBPS        0x00

esp_err_t pn532_get_firmware_version(pn532_io_handle_t, uint32_t*);
esp_err_t pn532_set_passive_activation_retries(pn532_io_handle_t, uint8_t);
esp_err_t pn532_read_passive_target_id(pn532_io_handle_t, uint8_t baud, uint8_t *uid, uint8_t *uid_len, int32_t timeout);

#ifdef __cplusplus
}
#endif
#endif