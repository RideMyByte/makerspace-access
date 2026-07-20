/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for Waveshare ESP32-C6 1.47" (172x320 ST7789)
 * Key changes: correct init sequence for this panel variant
 */

#include <stdlib.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "vernon_st7789t.h"

static const char *TAG = "st7789t";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    bool reset_level;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val;
    uint8_t colmod_cal;
} st7789t_panel_t;

static esp_err_t panel_st7789t_del(esp_lcd_panel_t *panel);
static esp_err_t panel_st7789t_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_st7789t_init(esp_lcd_panel_t *panel);
static esp_err_t panel_st7789t_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_st7789t_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_st7789t_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_st7789t_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_st7789t_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_st7789t_disp_on_off(esp_lcd_panel_t *panel, bool off);

esp_err_t esp_lcd_new_panel_st7789t(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_st7789t_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = ESP_OK;
    st7789t_panel_t *st7789t = calloc(1, sizeof(st7789t_panel_t));
    ESP_GOTO_ON_FALSE(st7789t, ESP_ERR_NO_MEM, err, TAG, "no mem");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        gpio_config(&io_conf);
    }

    switch (panel_dev_config->rgb_endian) {
    case LCD_RGB_ENDIAN_RGB: st7789t->madctl_val = 0; break;
    case LCD_RGB_ENDIAN_BGR: st7789t->madctl_val |= LCD_CMD_BGR_BIT; break;
    default: ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space"); break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: st7789t->colmod_cal = 0x55; st7789t->fb_bits_per_pixel = 16; break;
    case 18: st7789t->colmod_cal = 0x66; st7789t->fb_bits_per_pixel = 24; break;
    default: ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported bpp"); break;
    }

    st7789t->io = io;
    st7789t->reset_gpio_num = panel_dev_config->reset_gpio_num;
    st7789t->reset_level = panel_dev_config->flags.reset_active_high;
    st7789t->base.del = panel_st7789t_del;
    st7789t->base.reset = panel_st7789t_reset;
    st7789t->base.init = panel_st7789t_init;
    st7789t->base.draw_bitmap = panel_st7789t_draw_bitmap;
    st7789t->base.invert_color = panel_st7789t_invert_color;
    st7789t->base.set_gap = panel_st7789t_set_gap;
    st7789t->base.mirror = panel_st7789t_mirror;
    st7789t->base.swap_xy = panel_st7789t_swap_xy;
    st7789t->base.disp_on_off = panel_st7789t_disp_on_off;
    *ret_panel = &(st7789t->base);
    return ESP_OK;
err:
    if (st7789t) {
        if (panel_dev_config->reset_gpio_num >= 0) gpio_reset_pin(panel_dev_config->reset_gpio_num);
        free(st7789t);
    }
    return ret;
}

static esp_err_t panel_st7789t_del(esp_lcd_panel_t *panel) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    if (s->reset_gpio_num >= 0) gpio_reset_pin(s->reset_gpio_num);
    free(s);
    return ESP_OK;
}

static esp_err_t panel_st7789t_reset(esp_lcd_panel_t *panel) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    if (s->reset_gpio_num >= 0) {
        gpio_set_level(s->reset_gpio_num, s->reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(s->reset_gpio_num, !s->reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

static esp_err_t panel_st7789t_init(esp_lcd_panel_t *panel) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    esp_lcd_panel_io_handle_t io = s->io;

    // Exit sleep
    esp_lcd_panel_io_tx_param(io, LCD_CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));

    // MADCTL: start with BGR only (mirror/swap configured later)
    esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){s->madctl_val}, 1);
    // COLMOD: 16-bit color
    esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]){s->colmod_cal}, 1);

    // Waveshare-specific init sequence
    esp_lcd_panel_io_tx_param(io, 0xB2, (uint8_t[]){0x0C,0x0C,0x00,0x33,0x33}, 5);
    esp_lcd_panel_io_tx_param(io, 0xB7, (uint8_t[]){0x35}, 1);
    esp_lcd_panel_io_tx_param(io, 0xBB, (uint8_t[]){0x19}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC0, (uint8_t[]){0x2C}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC2, (uint8_t[]){0x01}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC3, (uint8_t[]){0x12}, 1);
    esp_lcd_panel_io_tx_param(io, 0xC4, (uint8_t[]){0x20}, 1);
    esp_lcd_panel_io_tx_param(io, 0xD0, (uint8_t[]){0xA4,0xA1}, 2);
    esp_lcd_panel_io_tx_param(io, 0xE0, (uint8_t[]){0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23}, 14);
    esp_lcd_panel_io_tx_param(io, 0xE1, (uint8_t[]){0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23}, 14);

    // Normal mode + display on
    esp_lcd_panel_io_tx_param(io, 0x21, NULL, 0); // INVON for Waveshare
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

static esp_err_t panel_st7789t_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    esp_lcd_panel_io_handle_t io = s->io;
    assert((x_start < x_end) && (y_start < y_end));

    x_start += s->x_gap; x_end += s->x_gap;
    y_start += s->y_gap; y_end += s->y_gap;

    esp_lcd_panel_io_tx_param(io, LCD_CMD_CASET, (uint8_t[]){
        (x_start>>8)&0xFF, x_start&0xFF, ((x_end-1)>>8)&0xFF, (x_end-1)&0xFF}, 4);
    esp_lcd_panel_io_tx_param(io, LCD_CMD_RASET, (uint8_t[]){
        (y_start>>8)&0xFF, y_start&0xFF, ((y_end-1)>>8)&0xFF, (y_end-1)&0xFF}, 4);
    size_t len = (x_end - x_start) * (y_end - y_start) * s->fb_bits_per_pixel / 8;
    esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, color_data, len);
    return ESP_OK;
}

static esp_err_t panel_st7789t_invert_color(esp_lcd_panel_t *panel, bool invert) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    esp_lcd_panel_io_tx_param(s->io, invert ? LCD_CMD_INVON : LCD_CMD_INVOFF, NULL, 0);
    return ESP_OK;
}

static esp_err_t panel_st7789t_mirror(esp_lcd_panel_t *panel, bool mx, bool my) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    if (mx) s->madctl_val |= LCD_CMD_MX_BIT; else s->madctl_val &= ~LCD_CMD_MX_BIT;
    if (my) s->madctl_val |= LCD_CMD_MY_BIT; else s->madctl_val &= ~LCD_CMD_MY_BIT;
    esp_lcd_panel_io_tx_param(s->io, LCD_CMD_MADCTL, (uint8_t[]){s->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_st7789t_swap_xy(esp_lcd_panel_t *panel, bool swap) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    if (swap) s->madctl_val |= LCD_CMD_MV_BIT; else s->madctl_val &= ~LCD_CMD_MV_BIT;
    esp_lcd_panel_io_tx_param(s->io, LCD_CMD_MADCTL, (uint8_t[]){s->madctl_val}, 1);
    return ESP_OK;
}

static esp_err_t panel_st7789t_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    s->x_gap = x_gap; s->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_st7789t_disp_on_off(esp_lcd_panel_t *panel, bool on) {
    st7789t_panel_t *s = __containerof(panel, st7789t_panel_t, base);
    esp_lcd_panel_io_tx_param(s->io, on ? LCD_CMD_DISPON : LCD_CMD_DISPOFF, NULL, 0);
    return ESP_OK;
}