#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/rmt_tx.h"

// ===== WiFi =====
#define WIFI_SSID "Halle1 IoT"
#define WIFI_PASSWORD "HalleEinsOfThings"

// ===== API =====
#define API_HOST "192.168.130.201"
#define API_PORT 80
#define API_KEY "udgw45z08awrjgsomng2p97sogjydggj4z"
#define API_BASE "/api/v1"

// ===== WS2812B =====
#define WS2812_PIN (gpio_num_t)8
#define WS2812_MAX_BRIGHTNESS 255

// ===== Display ST7789 (SPI) =====
// Waveshare ESP32-C6 1.47" pinout:
#define DISPLAY_MOSI (gpio_num_t)6
#define DISPLAY_SCLK (gpio_num_t)7
#define DISPLAY_CS   (gpio_num_t)14
#define DISPLAY_DC   (gpio_num_t)15
#define DISPLAY_RST  (gpio_num_t)21
#define DISPLAY_BL   (gpio_num_t)22
#define DISPLAY_WIDTH  320
#define DISPLAY_HEIGHT 172

// Display backlight brightness (0-100)
// WARNING: Keep at 50% or lower during use.
// Do not operate the screen at full brightness for extended periods.
// Excessive brightness increases the screen temperature, and overheating
// may cause dark shadows on the screen and affect normal display.
// See: https://docs.waveshare.com/ESP32-C6-LCD-1.47
#define DISPLAY_BRIGHTNESS 50

// ===== PN532 (SPI) =====
#define PN532_SPI_HOST    SPI2_HOST
#define PN532_MOSI        (gpio_num_t)9
#define PN532_MISO        (gpio_num_t)18
#define PN532_SCLK        (gpio_num_t)19
#define PN532_CS          (gpio_num_t)20
#define PN532_I2C_ADDR 0x24

// ===== Timing =====
#define POLL_INTERVAL_MS  500
#define DEBOUNCE_MS      2000
#define HTTP_TIMEOUT_MS  5000