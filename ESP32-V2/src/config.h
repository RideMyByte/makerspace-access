#pragma once

// ===== WiFi =====
#define WIFI_SSID "Halle1 IoT"
#define WIFI_PASSWORD "HalleEinsOfThings"

// ===== API =====
#define API_HOST "192.168.130.201"
#define API_PORT 80
#define API_KEY "udgw45z08awrjgsomng2p97sogjydggj4z"
#define API_BASE "/api/v1"

// ===== PN532 NFC (I2C) =====
#define PN532_IRQ -1   // Not used
#define PN532_RESET -1 // Not used

// ===== WS2812B =====
#define WS2812_PIN 8
#define WS2812_COUNT 1
#define WS2812_MAX_BRIGHTNESS 128

// ===== Display =====
#define DISPLAY_ROTATION 1  // Landscape: 320x172

// ===== Timing =====
#define POLL_INTERVAL_MS 500
#define HTTP_TIMEOUT_MS 5000
#define DEBOUNCE_MS 2000