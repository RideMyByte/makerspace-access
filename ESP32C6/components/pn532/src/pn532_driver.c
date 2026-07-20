// PN532 driver (from garag/esp-idf-pn532, MIT license)
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "pn532_driver.h"

static const char TAG[] = "pn532_driver";

#ifndef CONFIG_ENABLE_IRQ_ISR
static bool pn532_is_ready(pn532_io_handle_t io_handle);
#endif

#define PN532_COMMAND_BUFFER_LEN 64

esp_err_t pn532_init(pn532_io_handle_t io_handle)
{
    gpio_config_t io_conf;
    if (io_handle == NULL) return ESP_ERR_INVALID_ARG;

    if (io_handle->reset != GPIO_NUM_NC) {
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << io_handle->reset);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 1;
        gpio_config(&io_conf);
    }

    if (io_handle->irq != GPIO_NUM_NC) {
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << io_handle->irq);
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 1;
        gpio_config(&io_conf);
    }

    if (io_handle->reset != GPIO_NUM_NC) {
        pn532_reset(io_handle);
    }

    io_handle->isSAMConfigDone = false;
    esp_err_t err = io_handle->pn532_init_io(io_handle);
    if (err != ESP_OK) return err;

    err = pn532_SAM_config(io_handle);
    if (err != ESP_OK) return err;

    io_handle->isSAMConfigDone = true;

    if (io_handle->pn532_init_extra != NULL) {
        err = io_handle->pn532_init_extra(io_handle);
        if (err != ESP_OK) return err;
    }
    return err;
}

void pn532_release(pn532_io_handle_t io_handle) {
    if (io_handle == NULL) return;
    if (io_handle->pn532_release_io != NULL) io_handle->pn532_release_io(io_handle);
    if (io_handle->reset != GPIO_NUM_NC) gpio_reset_pin(io_handle->reset);
    if (io_handle->irq != GPIO_NUM_NC) gpio_reset_pin(io_handle->irq);
}

void pn532_delete_driver(pn532_io_handle_t io_handle) {
    if (io_handle == NULL) return;
    if (io_handle->pn532_release_driver != NULL) io_handle->pn532_release_driver(io_handle);
}

void pn532_reset(pn532_io_handle_t io_handle) {
    if (io_handle->reset == GPIO_NUM_NC) return;
    gpio_set_level(io_handle->reset, 0);
    vTaskDelay(400 / portTICK_PERIOD_MS);
    gpio_set_level(io_handle->reset, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    io_handle->isSAMConfigDone = false;
}

esp_err_t pn532_write_command(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmdlen, int timeout) {
    uint8_t command[256];
    uint8_t checksum = PN532_HOST_TO_PN532;
    int idx = 0;
    command[idx++] = PN532_STARTCODE1;
    command[idx++] = PN532_STARTCODE2;
    command[idx++] = (cmdlen + 1);
    command[idx++] = 0x100 - (cmdlen + 1);
    command[idx++] = PN532_HOST_TO_PN532;
    for (uint8_t i = 0; i < cmdlen; i++) { command[idx++] = cmd[i]; checksum += cmd[i]; }
    command[idx++] = ~checksum + 1;
    return io_handle->pn532_write(io_handle, command, idx, timeout);
}

esp_err_t pn532_read_data(pn532_io_handle_t io_handle, uint8_t *buffer, uint8_t length, int32_t timeout) {
    uint8_t local[256];
    memset(local, 0, sizeof(local));
    if (timeout == 0) timeout = -1;
    esp_err_t res = io_handle->pn532_read(io_handle, local, length, timeout);
    if (res != ESP_OK) return res;
    memcpy(buffer, local, length);
    return ESP_OK;
}

#ifndef CONFIG_ENABLE_IRQ_ISR
static bool pn532_is_ready(pn532_io_handle_t io_handle) {
    return gpio_get_level(io_handle->irq) == 0;
}
#endif

static esp_err_t pn532_poll_ready(pn532_io_handle_t io_handle, int32_t timeout) {
    TickType_t start = xTaskGetTickCount();
    TickType_t to = (timeout > 0) ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
    esp_rom_delay_us(1000);
    bool ready = false;
    while (!ready && (xTaskGetTickCount() - start) <= to) {
        ready = (ESP_OK == io_handle->pn532_is_ready(io_handle));
        if (!ready) vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ready ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t pn532_wait_ready(pn532_io_handle_t io_handle, int32_t timeout) {
    if (io_handle->irq == GPIO_NUM_NC) {
        if (io_handle->pn532_is_ready != NULL) return pn532_poll_ready(io_handle, timeout);
        return ESP_OK;
    }
    uint16_t timer = 0;
    while (gpio_get_level(io_handle->irq) != 0) {
        if (timeout != 0) { timer += 10; if (timer > timeout) return ESP_ERR_TIMEOUT; }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    return ESP_OK;
}

esp_err_t pn532_SAM_config(pn532_io_handle_t io_handle) {
    uint8_t resp[16];
    static const uint8_t sam[] = { 0x14, 0x01, 0x00, 0x01 };
    esp_err_t err = pn532_send_command_wait_ack(io_handle, sam, sizeof(sam), 1000);
    if (err != ESP_OK) return err;
    err = pn532_wait_ready(io_handle, 100);
    if (err != ESP_OK) return err;
    err = pn532_read_data(io_handle, resp, 10, PN532_READ_TIMEOUT);
    if (err != ESP_OK) return err;
    return (resp[6] == 0x15) ? ESP_OK : ESP_FAIL;
}

esp_err_t pn532_send_command_wait_ack(pn532_io_handle_t io_handle, const uint8_t *cmd, uint8_t cmd_len, int32_t timeout) {
    esp_err_t err = pn532_write_command(io_handle, cmd, cmd_len, timeout);
    if (err != ESP_OK) return err;
    err = pn532_wait_ready(io_handle, timeout);
    if (err != ESP_OK) return err;
    return pn532_read_ack(io_handle);
}

esp_err_t pn532_read_ack(pn532_io_handle_t io_handle) {
    uint8_t ack[6];
    static const uint8_t ACK[] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    esp_err_t err = pn532_read_data(io_handle, ack, sizeof(ACK), PN532_READ_TIMEOUT);
    if (err != ESP_OK) return err;
    return (memcmp(ack, ACK, sizeof(ACK)) == 0) ? ESP_OK : ESP_FAIL;
}