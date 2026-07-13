#pragma once

// ===== WiFi =====
#define WIFI_SSID "Halle1 IoT"
#define WIFI_PASSWORD "HalleEinsOfThings"

// ===== API =====
#define API_HOST "192.168.130.201"
#define API_PORT 80
#define API_KEY "udgw45z08awrjgsomng2p97sogjydggj4z"
#define API_BASE "/api/v1"

// ===== I2C (PN532 NFC) =====
#define I2C_SDA 19
#define I2C_SCL 20
#define PN532_I2C_ADDR 0x48  // 7-bit I2C address

// ===== WS2812B LED =====
#define WS2812_PIN 8
#define WS2812_COUNT 1
#define WS2812_MAX_BRIGHTNESS 128

// ===== Display (ST7789V) =====
#define DISPLAY_MOSI 4
#define DISPLAY_SCLK 7
#define DISPLAY_CS   6
#define DISPLAY_DC   5
#define DISPLAY_RST  3
#define DISPLAY_BL   2
#define DISPLAY_WIDTH  320   // Landscape: 320x172
#define DISPLAY_HEIGHT 172
#define DISPLAY_ROTATION 1

// ===== Timing =====
#define POLL_INTERVAL_MS 500
#define HTTP_TIMEOUT_MS 5000
#define DEBOUNCE_MS 2000