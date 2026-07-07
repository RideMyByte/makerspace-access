#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#include "config.h"

// ===== Globals =====
MFRC522 rfid(RC522_SS_PIN, RC522_RST_PIN);
CRGB leds[WS2812_COUNT];

// LED animation state
enum AnimState : uint8_t {
  ANIM_IDLE = 0,
  ANIM_RAINBOW = 3,
  ANIM_FADE_UP_GREEN = 1,
  ANIM_FADE_GREEN_TO_BLUE = 2,
  ANIM_PULSE_RED = 4,
};

AnimState currentAnim = ANIM_IDLE;
unsigned long animStartMs = 0;

// Non-blocking timers
unsigned long lastCardCheckMs = 0;
unsigned long cardDebounceUntilMs = 0;

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
      // Gentle white pulsing at 1/5 brightness
      uint16_t cycle = (now / 4000) % 1000;  // 4s cycle
      uint8_t brightness;
      if (cycle < 500) {
        brightness = map(cycle, 0, 499, 0, 51);
      } else {
        brightness = map(cycle, 500, 999, 51, 0);
      }
      setLed(CRGB::White, brightness);
      break;
    }

    case ANIM_FADE_UP_GREEN: {
      if (elapsed >= 3000) {
        ledOff();
        currentAnim = ANIM_IDLE;
        return;
      }
      uint8_t brightness = map(elapsed, 0, 3000, 0, WS2812_MAX_BRIGHTNESS);
      setLed(CRGB::Green, brightness);
      break;
    }

    case ANIM_FADE_GREEN_TO_BLUE: {
      if (elapsed >= 3000) {
        setLed(CRGB::Blue);
        currentAnim = ANIM_IDLE;
        return;
      }
      uint8_t progress = map(elapsed, 0, 3000, 0, 255);
      CRGB color = blend(CRGB::Green, CRGB::Blue, progress);
      setLed(color);
      break;
    }

    case ANIM_RAINBOW: {
      if (elapsed >= 3000) {
        currentAnim = ANIM_IDLE;
        return;
      }
      uint8_t hue = map(elapsed, 0, 3000, 0, 255);
      setLed(CHSV(hue, 255, 255));
      break;
    }

    case ANIM_PULSE_RED: {
      if (elapsed >= 5000) {
        ledOff();
        currentAnim = ANIM_IDLE;
        return;
      }
      uint16_t cycle = elapsed % 800;
      uint8_t brightness;
      if (cycle < 400) {
        brightness = map(cycle, 0, 399, 50, WS2812_MAX_BRIGHTNESS);
      } else {
        brightness = map(cycle, 400, 799, WS2812_MAX_BRIGHTNESS, 50);
      }
      setLed(CRGB::Red, brightness);
      break;
    }
  }
}

// ===== WiFi =====
void connectWifi() {
  Serial.printf("Connecting to WiFi \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
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

// ===== NFC =====
String readCardUid() {
  if (!rfid.PICC_IsNewCardPresent()) return "";
  if (!rfid.PICC_ReadCardSerial()) return "";

  String uid;
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  return uid;
}

// ===== Process NFC =====
void processCard(const String& nfcId) {
  Serial.printf("Card: %s\n", nfcId.c_str());

  // Get member by NFC
  String path = "/members/nfc/";
  path += nfcId;
  String response;
  int code = apiRequest("GET", path, "", &response);

  if (code <= 0 || response.length() == 0) {
    Serial.println("API error");
    startAnimation(ANIM_PULSE_RED);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.println("JSON parse error");
    startAnimation(ANIM_PULSE_RED);
    return;
  }

  // Unknown NFC → submit as pending
  if (doc["detail"].is<String>() && doc["detail"].as<String>().length() > 0) {
    JsonDocument p;
    p["nfc_id"] = nfcId;
    String body;
    serializeJson(p, body);
    int pc = apiRequest("POST", "/pending-nfc", body);
    if (pc == 201 || pc == 409) {
      Serial.println("Pending NFC submitted");
      startAnimation(ANIM_RAINBOW);
    } else {
      Serial.printf("Pending NFC failed: HTTP %d\n", pc);
      startAnimation(ANIM_PULSE_RED);
    }
    return;
  }

  // Known member
  bool safetyValid = doc["safety_briefing_valid"] | false;
  bool isPresent = doc["is_present"] | false;

  if (!safetyValid) {
    Serial.println("Safety invalid → red pulse");
    startAnimation(ANIM_PULSE_RED);
    return;
  }

  if (isPresent) {
    // Check out
    JsonDocument co;
    co["nfc_id"] = nfcId;
    String body;
    serializeJson(co, body);
    int c = apiRequest("POST", "/members/check-out", body, &response);
    if (c == 200) {
      Serial.println("Checked out → green→blue");
      startAnimation(ANIM_FADE_GREEN_TO_BLUE);
    } else {
      Serial.printf("Check-out failed: HTTP %d\n", c);
      startAnimation(ANIM_PULSE_RED);
    }
  } else {
    // Check in
    JsonDocument ci;
    ci["nfc_id"] = nfcId;
    String body;
    serializeJson(ci, body);
    int c = apiRequest("POST", "/members/check-in", body, &response);
    if (c == 200) {
      Serial.println("Checked in → green fade");
      startAnimation(ANIM_FADE_UP_GREEN);
    } else {
      Serial.printf("Check-in failed: HTTP %d\n", c);
      startAnimation(ANIM_PULSE_RED);
    }
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nMakerSpace NFC Terminal starting...");

  FastLED.addLeds<WS2812B, WS2812_PIN, GRB>(leds, WS2812_COUNT);
  FastLED.setBrightness(WS2812_MAX_BRIGHTNESS);
  setLed(CRGB::Purple);
  delay(500);
  ledOff();

  SPI.begin();
  rfid.PCD_Init();
  delay(50);
  rfid.PCD_DumpVersionToSerial();

  connectWifi();

  // Diagnostics
  Serial.println("--- API diagnostics ---");
  IPAddress apiIp;
  if (WiFi.hostByName(API_HOST, apiIp)) {
    Serial.printf("✅ DNS: %s → %s\n", API_HOST, apiIp.toString().c_str());
    WiFiClient test;
    if (test.connect(apiIp, API_PORT)) {
      Serial.printf("✅ TCP connect %s:%d OK\n", API_HOST, API_PORT);
      test.stop();
    } else {
      Serial.printf("❌ TCP connect failed\n");
    }
    HTTPClient http;
    http.begin(buildUrl("/health"));
    int hc = http.GET();
    if (hc > 0) {
      Serial.printf("✅ Health: HTTP %d\n", hc);
    }
    http.end();
    http.begin(buildUrl("/members/present"));
    http.addHeader("X-API-Key", API_KEY);
    int ac = http.GET();
    if (ac == 200) Serial.println("✅ API key valid");
    else Serial.printf("⚠ API key check: HTTP %d\n", ac);
    http.end();
  } else {
    Serial.printf("❌ DNS resolution failed\n");
  }
  Serial.println("--- Ready ---");

  setLed(CRGB::Green, 80);
  delay(200);
  ledOff();
}

// ===== Loop =====
void loop() {
  updateAnimation();

  unsigned long now = millis();

  if (now < cardDebounceUntilMs) return;
  if (now - lastCardCheckMs < POLL_INTERVAL_MS) return;
  lastCardCheckMs = now;

  String nfcId = readCardUid();
  if (nfcId.length() == 0) return;

  processCard(nfcId);
  cardDebounceUntilMs = millis() + DEBOUNCE_MS;
}