#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <TFT_eSPI.h>
#include <FastLED.h>

#include "config.h"

// ===== Display =====
TFT_eSPI tft = TFT_eSPI();

// ===== PN532 NFC (I2C) =====
Adafruit_PN532 nfc = Adafruit_PN532(PN532_IRQ);

// ===== LED =====
CRGB leds[WS2812_COUNT];

// ===== Animation state =====
enum AnimState : uint8_t {
  ANIM_IDLE = 0,
  ANIM_BLINK_GREEN,
  ANIM_BLINK_RED,
  ANIM_RAINBOW,
  ANIM_PINK_BLINK,
  ANIM_BLUE_BLINK,
};

AnimState currentAnim = ANIM_IDLE;
unsigned long animStartMs = 0;

// ===== UI state =====
enum ScreenState : uint8_t {
  SCREEN_IDLE,
  SCREEN_CHECKED_IN,
  SCREEN_CHECKED_OUT,
  SCREEN_UNKNOWN,
};

ScreenState screenState = SCREEN_IDLE;
String screenFirstName = "";
unsigned long screenUntilMs = 0;

// Timers
unsigned long lastCardCheckMs = 0;
unsigned long cardDebounceUntilMs = 0;
unsigned long lastWifiCheckMs = 0;
bool wifiWasConnected = false;

// ===== LED helpers =====
void startAnimation(AnimState anim) {
  currentAnim = anim;
  animStartMs = millis();
}

void setLed(CRGB color, uint8_t brightness = WS2812_MAX_BRIGHTNESS) {
  leds[0] = color;
  leds[0].nscale8(brightness);
  FastLED.show();
}

void ledOff() {
  leds[0] = CRGB::Black;
  FastLED.show();
}

void updateAnimation() {
  unsigned long now = millis();
  unsigned long elapsed = now - animStartMs;

  switch (currentAnim) {
    case ANIM_IDLE: {
      uint16_t cycle = now % 4000;
      uint8_t brightness;
      if (cycle < 2000) {
        brightness = map(cycle, 0, 1999, 0, WS2812_MAX_BRIGHTNESS);
      } else {
        brightness = map(cycle, 2000, 3999, WS2812_MAX_BRIGHTNESS, 0);
      }
      setLed(CRGB::White, brightness);
      break;
    }
    case ANIM_BLINK_GREEN: {
      if (elapsed >= 5000) { currentAnim = ANIM_IDLE; return; }
      uint8_t phase = (elapsed / 150) % 2;
      setLed(CRGB::Green, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }
    case ANIM_BLINK_RED: {
      if (elapsed >= 5000) { currentAnim = ANIM_IDLE; return; }
      uint8_t phase = (elapsed / 150) % 2;
      setLed(CRGB::Red, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }
    case ANIM_RAINBOW: {
      if (elapsed >= 3000) { currentAnim = ANIM_IDLE; return; }
      uint8_t hue = map(elapsed, 0, 3000, 0, 255);
      setLed(CHSV(hue, 255, 255));
      break;
    }
    case ANIM_PINK_BLINK: {
      if (elapsed >= 8000) { currentAnim = ANIM_IDLE; return; }
      uint8_t phase = (elapsed / 250) % 2;
      setLed(CRGB(255, 20, 147), phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }
    case ANIM_BLUE_BLINK: {
      uint8_t phase = (elapsed / 250) % 2;
      setLed(CRGB::Blue, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }
  }
}

// ===== Display helpers =====
void drawIdleScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.setTextSize(3);
  tft.drawCentreString("Hallo! :)", tft.width() / 2, 30, 4);

  tft.setTextSize(6);
  struct tm timeinfo;
  char timeStr[9];
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  } else {
    snprintf(timeStr, sizeof(timeStr), "--:--");
  }
  tft.drawCentreString(timeStr, tft.width() / 2, 85, 4);
}

void drawCheckinScreen(const String& firstName) {
  tft.fillScreen(TFT_GREEN);
  tft.setTextColor(TFT_WHITE, TFT_GREEN);
  tft.setTextSize(4);
  tft.drawCentreString(firstName, tft.width() / 2, 55, 4);
}

void drawCheckoutScreen(const String& firstName) {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(4);
  tft.drawCentreString(firstName, tft.width() / 2, 30, 4);
  tft.setTextSize(3);
  tft.drawCentreString("Bye", tft.width() / 2, 90, 4);
}

void drawUnknownScreen() {
  tft.fillScreen(TFT_ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_ORANGE);
  tft.setTextSize(3);
  tft.drawCentreString("Neue ID", tft.width() / 2, 40, 4);
  tft.drawCentreString("gescannt", tft.width() / 2, 85, 4);
}

void updateScreen() {
  if (screenState != SCREEN_IDLE && millis() > screenUntilMs) {
    screenState = SCREEN_IDLE;
    drawIdleScreen();
  }
}

void showTemporaryScreen(ScreenState state, const String& firstName, unsigned long durationMs) {
  screenState = state;
  screenFirstName = firstName;
  screenUntilMs = millis() + durationMs;

  switch (state) {
    case SCREEN_CHECKED_IN: drawCheckinScreen(firstName); break;
    case SCREEN_CHECKED_OUT: drawCheckoutScreen(firstName); break;
    case SCREEN_UNKNOWN: drawUnknownScreen(); break;
    default: break;
  }
}

// ===== WiFi =====
void connectWifi() {
  Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    uint8_t phase = (millis() / 250) % 2;
    setLed(CRGB::Blue, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
    if (millis() - start > 30000) {
      Serial.println("\nWiFi connection timeout!");
      break;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
    wifiWasConnected = true;
  }
}

void checkWifiStatus() {
  unsigned long now = millis();
  if (now - lastWifiCheckMs < 2000) return;
  lastWifiCheckMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    if (currentAnim == ANIM_IDLE || currentAnim == ANIM_BLUE_BLINK) {
      startAnimation(ANIM_BLUE_BLINK);
    }
    if (wifiWasConnected) {
      Serial.println("WiFi lost, reconnecting...");
      wifiWasConnected = false;
      WiFi.reconnect();
    }
  } else {
    if (currentAnim == ANIM_BLUE_BLINK) currentAnim = ANIM_IDLE;
    wifiWasConnected = true;
  }
}

// ===== HTTP helpers =====
String buildUrl(const String& path) {
  String url = "http://";
  url += API_HOST;
  url += ":";
  url += API_PORT;
  url += API_BASE;
  url += path;
  return url;
}

int apiRequest(const String& method, const String& path, const String& jsonBody = "", String* response = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return -1;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(buildUrl(path));
  http.addHeader("X-API-Key", API_KEY);

  int code = 0;
  if (method == "GET") {
    code = http.GET();
  } else if (method == "POST") {
    if (jsonBody.length() > 0) {
      http.addHeader("Content-Type", "application/json");
      code = http.POST(jsonBody);
    } else {
      code = http.POST("");
    }
  } else {
    http.end();
    return 0;
  }

  if (code > 0) {
    if (response) *response = http.getString();
  } else {
    Serial.printf("HTTP %s %s failed: %s\n", method.c_str(), path.c_str(), http.errorToString(code).c_str());
  }
  http.end();
  return code;
}

// ===== NFC (PN532) =====
bool readNfcId(String& outUid) {
  uint8_t uid[7];
  uint8_t uidLen;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
    return false;
  }

  String uidStr;
  for (uint8_t i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
  }
  uidStr.toUpperCase();

  // Wait for card removal
  while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
    delay(50);
  }

  outUid = uidStr;
  return true;
}

// ===== Process NFC =====
void processCard(const String& nfcId) {
  Serial.printf("Card: %s\n", nfcId.c_str());

  if (WiFi.status() != WL_CONNECTED) {
    startAnimation(ANIM_BLUE_BLINK);
    return;
  }

  String path = "/members/nfc/";
  path += nfcId;
  String response;
  int code = apiRequest("GET", path, "", &response);

  if (code <= 0 || code == 401 || response.length() == 0) {
    startAnimation(ANIM_PINK_BLINK);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) { startAnimation(ANIM_PINK_BLINK); return; }

  if (code == 404 && doc["detail"].is<String>()) {
    JsonDocument p;
    p["nfc_id"] = nfcId;
    String body;
    serializeJson(p, body);
    int pc = apiRequest("POST", "/pending-nfc", body);
    if (pc == 201 || pc == 409) {
      startAnimation(ANIM_RAINBOW);
      showTemporaryScreen(SCREEN_UNKNOWN, "", 3000);
    } else {
      startAnimation(ANIM_PINK_BLINK);
    }
    return;
  }

  if (code != 200) { startAnimation(ANIM_PINK_BLINK); return; }

  bool isPresent = doc["is_present"] | false;
  String firstName = doc["first_name"] | "";

  if (isPresent) {
    JsonDocument co;
    co["nfc_id"] = nfcId;
    String body;
    serializeJson(co, body);
    int c = apiRequest("POST", "/members/check-out", body, &response);
    if (c == 200) {
      startAnimation(ANIM_BLINK_RED);
      showTemporaryScreen(SCREEN_CHECKED_OUT, firstName, 5000);
    } else {
      startAnimation(ANIM_PINK_BLINK);
    }
  } else {
    JsonDocument ci;
    ci["nfc_id"] = nfcId;
    String body;
    serializeJson(ci, body);
    int c = apiRequest("POST", "/members/check-in", body, &response);
    if (c == 200) {
      startAnimation(ANIM_BLINK_GREEN);
      showTemporaryScreen(SCREEN_CHECKED_IN, firstName, 5000);
    } else {
      startAnimation(ANIM_PINK_BLINK);
    }
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nMakerSpace NFC Terminal V2 starting...");

  FastLED.addLeds<WS2812B, WS2812_PIN, GRB>(leds, WS2812_COUNT);
  FastLED.setBrightness(WS2812_MAX_BRIGHTNESS);
  setLed(CRGB::Purple);
  delay(500);
  ledOff();

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  Wire.begin();
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (versiondata) {
    Serial.printf("PN532 found, FW version: %d.%d\n",
      (versiondata >> 24) & 0xFF, (versiondata >> 16) & 0xFF);
    nfc.SAMConfig();
  } else {
    Serial.println("PN532 not found!");
  }

  connectWifi();
  drawIdleScreen();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.println("--- Ready ---");
}

// ===== Loop =====
void loop() {
  updateAnimation();

  unsigned long now = millis();

  updateScreen();

  if (screenState == SCREEN_IDLE && (now % 30000 < POLL_INTERVAL_MS)) {
    drawIdleScreen();
  }

  checkWifiStatus();

  if (now < cardDebounceUntilMs) return;
  if (now - lastCardCheckMs < POLL_INTERVAL_MS) return;
  lastCardCheckMs = now;

  String nfcId;
  if (!readNfcId(nfcId)) return;
  if (nfcId.length() == 0) return;

  processCard(nfcId);
  cardDebounceUntilMs = millis() + DEBOUNCE_MS;
}