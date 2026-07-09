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

// ===== LED animation states =====
enum AnimState : uint8_t {
  ANIM_IDLE = 0,          // White pulsating – waiting for scan
  ANIM_CHECK_IN_OK,       // Green fast blink 3s – login successful
  ANIM_SAFETY_OK,         // Green solid on 3s – safety briefing valid
  ANIM_SAFETY_FAIL,       // Red fast blink 5s – safety briefing missing/expired
  ANIM_CHECK_OUT,         // Green→red→idle fade – logout sequence
  ANIM_UNKNOWN_CARD,      // Rainbow starting at white – unknown NFC
  ANIM_API_ERROR,         // Pink blinking – API unreachable or wrong key
  ANIM_NO_WIFI,           // Blue blinking – no WiFi connection
  ANIM_ERROR,             // Red pulse – generic error
};

AnimState currentAnim = ANIM_IDLE;
unsigned long animStartMs = 0;

// Non-blocking timers
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

    // ─── IDLE: white pulsating at full brightness, 4s cycle ───
    case ANIM_IDLE: {
      uint16_t cycle = now % 4000;  // 0…3999 ms
      uint8_t brightness;
      if (cycle < 2000) {
        brightness = map(cycle, 0, 1999, 0, WS2812_MAX_BRIGHTNESS);
      } else {
        brightness = map(cycle, 2000, 3999, WS2812_MAX_BRIGHTNESS, 0);
      }
      setLed(CRGB::White, brightness);
      break;
    }

    // ─── CHECK-IN OK: green fast blink 3s ───
    case ANIM_CHECK_IN_OK: {
      if (elapsed >= 3000) {
        // Transition to safety check animation
        startAnimation(ANIM_SAFETY_OK);
        return;
      }
      uint8_t phase = (elapsed / 150) % 2;  // ~150ms per half-cycle
      setLed(CRGB::Green, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }

    // ─── SAFETY OK: green solid on 3s ───
    case ANIM_SAFETY_OK: {
      if (elapsed >= 3000) {
        currentAnim = ANIM_IDLE;
        return;
      }
      setLed(CRGB::Green, WS2812_MAX_BRIGHTNESS);
      break;
    }

    // ─── SAFETY FAIL: red fast blink 5s ───
    case ANIM_SAFETY_FAIL: {
      if (elapsed >= 5000) {
        currentAnim = ANIM_IDLE;
        return;
      }
      uint8_t phase = (elapsed / 150) % 2;
      setLed(CRGB::Red, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }

    // ─── CHECK-OUT: green→red 2s → red solid 3s → fade to idle 2s ───
    case ANIM_CHECK_OUT: {
      // Phase 1: green fade → red (2s)
      if (elapsed < 2000) {
        uint8_t blendVal = map(elapsed, 0, 1999, 0, 255);
        CRGB color = blend(CRGB::Green, CRGB::Red, blendVal);
        setLed(color);
        return;
      }
      // Phase 2: red solid on (3s)
      if (elapsed < 5000) {
        setLed(CRGB::Red, WS2812_MAX_BRIGHTNESS);
        return;
      }
      // Phase 3: fade red → off (2s) → idle
      if (elapsed < 7000) {
        uint8_t brightness = map(elapsed, 5000, 6999, WS2812_MAX_BRIGHTNESS, 0);
        setLed(CRGB::Red, brightness);
        return;
      }
      currentAnim = ANIM_IDLE;
      break;
    }

    // ─── UNKNOWN CARD: rainbow starting from white (3s) ───
    case ANIM_UNKNOWN_CARD: {
      if (elapsed >= 3000) {
        currentAnim = ANIM_IDLE;
        return;
      }
      // Start from white (hue=0, low saturation) and sweep to full rainbow
      uint8_t hue = map(elapsed, 0, 3000, 0, 255);
      uint8_t sat = map(elapsed, 0, 1000, 0, 255);  // fade into color
      if (sat > 255) sat = 255;
      setLed(CHSV(hue, sat, 255));
      break;
    }

    // ─── API ERROR: pink blinking (500ms cycle) ───
    case ANIM_API_ERROR: {
      if (elapsed >= 8000) {
        currentAnim = ANIM_IDLE;
        return;
      }
      uint8_t phase = (elapsed / 250) % 2;
      setLed(CRGB(255, 20, 147), phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);  // DeepPink
      break;
    }

    // ─── NO WIFI: blue blinking (500ms cycle) ───
    case ANIM_NO_WIFI: {
      uint8_t phase = (elapsed / 250) % 2;
      setLed(CRGB::Blue, phase == 0 ? WS2812_MAX_BRIGHTNESS : 0);
      break;
    }

    // ─── GENERIC ERROR: red pulse (5s) ───
    case ANIM_ERROR: {
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
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    // Show blue blinking during connection attempt
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

// Check WiFi status and signal disconnection via LED
void checkWifiStatus() {
  unsigned long now = millis();
  if (now - lastWifiCheckMs < 2000) return;
  lastWifiCheckMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    if (currentAnim == ANIM_IDLE || currentAnim == ANIM_NO_WIFI) {
      startAnimation(ANIM_NO_WIFI);
    }
    // Try to reconnect
    if (wifiWasConnected) {
      Serial.println("WiFi lost, reconnecting...");
      wifiWasConnected = false;
      WiFi.reconnect();
    }
  } else {
    if (currentAnim == ANIM_NO_WIFI) {
      currentAnim = ANIM_IDLE;
    }
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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi – cannot make API request");
    return -1;
  }

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

  // Check WiFi first
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No WiFi – cannot process card");
    startAnimation(ANIM_NO_WIFI);
    return;
  }

  // Get member by NFC
  String path = "/members/nfc/";
  path += nfcId;
  String response;
  int code = apiRequest("GET", path, "", &response);

  // API error or connection failure
  if (code <= 0) {
    Serial.println("Connection error – API unreachable");
    startAnimation(ANIM_API_ERROR);
    return;
  }

  // Wrong API key (401)
  if (code == 401) {
    Serial.println("Wrong API key");
    startAnimation(ANIM_API_ERROR);
    return;
  }

  // Empty response
  if (response.length() == 0) {
    Serial.println("Empty API response");
    startAnimation(ANIM_API_ERROR);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.println("JSON parse error");
    startAnimation(ANIM_API_ERROR);
    return;
  }

  // 404 with detail → unknown NFC card, not an API error
  if (code == 404 && doc["detail"].is<String>()) {
    JsonDocument p;
    p["nfc_id"] = nfcId;
    String body;
    serializeJson(p, body);
    int pc = apiRequest("POST", "/pending-nfc", body);
    if (pc == 201 || pc == 409) {
      Serial.println("Unknown card – submitted as pending NFC → rainbow");
      startAnimation(ANIM_UNKNOWN_CARD);
    } else {
      Serial.printf("Pending NFC submission failed: HTTP %d\n", pc);
      startAnimation(ANIM_API_ERROR);
    }
    return;
  }

  // Any other unexpected HTTP error
  if (code != 200) {
    Serial.printf("API error: HTTP %d\n", code);
    startAnimation(ANIM_API_ERROR);
    return;
  }

  // Known member
  bool safetyValid = doc["safety_briefing_valid"] | false;
  bool isPresent = doc["is_present"] | false;

  if (isPresent) {
    // ─── CHECK OUT ───
    JsonDocument co;
    co["nfc_id"] = nfcId;
    String body;
    serializeJson(co, body);
    int c = apiRequest("POST", "/members/check-out", body, &response);
    if (c == 200) {
      Serial.println("Checked out → green→red sequence");
      startAnimation(ANIM_CHECK_OUT);
    } else {
      Serial.printf("Check-out failed: HTTP %d\n", c);
      startAnimation(ANIM_ERROR);
    }
  } else {
    // ─── CHECK IN ───
    if (!safetyValid) {
      Serial.println("Safety invalid → red fast blink 5s, skip check-in");
      startAnimation(ANIM_SAFETY_FAIL);
      return;
    }

    JsonDocument ci;
    ci["nfc_id"] = nfcId;
    String body;
    serializeJson(ci, body);
    int c = apiRequest("POST", "/members/check-in", body, &response);
    if (c == 200) {
      Serial.println("Checked in → green blink, then safety check");
      startAnimation(ANIM_CHECK_IN_OK);
      // ANIM_CHECK_IN_OK will auto-transition to ANIM_SAFETY_OK after 3s
    } else {
      Serial.printf("Check-in failed: HTTP %d\n", c);
      startAnimation(ANIM_ERROR);
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
  if (WiFi.status() == WL_CONNECTED) {
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
      } else {
        Serial.println("❌ Health check failed – showing API error");
        startAnimation(ANIM_API_ERROR);
      }
      http.end();
      http.begin(buildUrl("/members/present"));
      http.addHeader("X-API-Key", API_KEY);
      int ac = http.GET();
      if (ac == 200) {
        Serial.println("✅ API key valid");
      } else if (ac == 401) {
        Serial.println("❌ Wrong API key – pink blinking");
        startAnimation(ANIM_API_ERROR);
      } else {
        Serial.printf("⚠ API key check: HTTP %d\n", ac);
      }
      http.end();
    } else {
      Serial.printf("❌ DNS resolution failed\n");
    }
  } else {
    Serial.println("❌ No WiFi – will retry in loop");
    startAnimation(ANIM_NO_WIFI);
  }
  Serial.println("--- Ready ---");

  if (currentAnim == ANIM_IDLE) {
    setLed(CRGB::Green, 80);
    delay(200);
    ledOff();
  }
}

// ===== Loop =====
void loop() {
  updateAnimation();
  checkWifiStatus();

  unsigned long now = millis();

  if (now < cardDebounceUntilMs) return;
  if (now - lastCardCheckMs < POLL_INTERVAL_MS) return;
  lastCardCheckMs = now;

  String nfcId = readCardUid();
  if (nfcId.length() == 0) return;

  processCard(nfcId);
  cardDebounceUntilMs = millis() + DEBOUNCE_MS;
}