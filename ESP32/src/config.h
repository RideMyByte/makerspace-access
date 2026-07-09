#pragma once

// ===== WiFi =====
#define WIFI_SSID "Halle1 IoT"
#define WIFI_PASSWORD "HalleEinsOfThings"

// ===== API =====
#define API_HOST "192.168.130.201"
#define API_PORT 80
#define API_KEY "udgw45z08awrjgsomng2p97sogjydggj4z"
#define API_BASE "/api/v1"

// ===== RFID-RC522 (SPI) =====
#define RC522_SS_PIN 5
#define RC522_RST_PIN 21

// ===== WS2812B =====
#define WS2812_PIN 13
#define WS2812_COUNT 1
#define WS2812_MAX_BRIGHTNESS 255

// ===== Timing =====
#define POLL_INTERVAL_MS 500
#define HTTP_TIMEOUT_MS 5000
#define DEBOUNCE_MS 2000
