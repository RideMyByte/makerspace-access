#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <lvgl.h>
#include <time.h>

#include "pin_config.h"
#include "ui/lv_setup.h"
#include "ui/ui.h"

// PN532 I2C
Adafruit_PN532 nfc(-1, -1);

// State
enum ScreenState { SCREEN_IDLE, SCREEN_CHECK_IN_OK, SCREEN_CHECK_OUT, SCREEN_UNKNOWN };
ScreenState screenState = SCREEN_IDLE;
unsigned long screenStartMs = 0, lastCardCheck = 0, cardDebounce = 0;

void connectWiFi() {
    Serial.print("WiFi"); WiFi.mode(WIFI_STA); WiFi.begin("Halle1 IoT", "HalleEinsOfThings");
    int r = 0; while (WiFi.status() != WL_CONNECTED && r < 30) { delay(500); Serial.print("."); r++; }
    Serial.println(WiFi.status() == WL_CONNECTED ? " OK" : " FAIL");
}

int apiReq(const String& m, const String& p, const String& b, String& r) {
    if (WiFi.status() != WL_CONNECTED) return -1;
    HTTPClient h; h.setTimeout(5000);
    h.begin("http://192.168.130.201:80/api/v1" + p);
    h.addHeader("X-API-Key", "udgw45z08awrjgsomng2p97sogjydggj4z");
    int c = 0;
    if (m == "GET") c = h.GET();
    else { h.addHeader("Content-Type", "application/json"); c = h.POST(b); }
    if (c > 0) r = h.getString();
    h.end(); return c;
}

void showCheckIn(const String& n, bool v, const String& sd) {
    screenState = SCREEN_CHECK_IN_OK; screenStartMs = millis();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(v ? 0x00FF00 : 0xFF0000), LV_STATE_DEFAULT);
    lv_obj_t *l1 = lv_label_create(scr); lv_label_set_text(l1, "Hallo!"); lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_t *l2 = lv_label_create(scr); lv_label_set_text(l2, n.c_str()); lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
    lv_obj_t *l3 = lv_label_create(scr); lv_label_set_text(l3, ("Unterweisung: " + sd).c_str()); lv_obj_align(l3, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_scr_load(scr);
}

void showCheckOut(const String& n, unsigned long ds) {
    screenState = SCREEN_CHECK_OUT; screenStartMs = millis();
    int h = ds/3600, m = (ds%3600)/60; char buf[32]; snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    lv_obj_t *l1 = lv_label_create(scr); lv_label_set_text(l1, "Bye"); lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l1, lv_color_hex(0xFF0000), 0);
    lv_obj_t *l2 = lv_label_create(scr); lv_label_set_text(l2, n.c_str()); lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
    lv_obj_t *l3 = lv_label_create(scr); lv_label_set_text(l3, ("Sitzung: " + String(buf)).c_str()); lv_obj_align(l3, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_text_color(l3, lv_color_hex(0xFFFF00), 0);
    lv_scr_load(scr);
}

void showUnknown(const String& id) {
    screenState = SCREEN_UNKNOWN; screenStartMs = millis();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x808080), LV_STATE_DEFAULT);
    lv_obj_t *l1 = lv_label_create(scr); lv_label_set_text(l1, "Unbekannte ID"); lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
    lv_obj_t *l2 = lv_label_create(scr); lv_label_set_text(l2, id.c_str()); lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 100);
    lv_scr_load(scr);
}

void processCard(const String& nfcId) {
    if (WiFi.status() != WL_CONNECTED) return;
    String path = "/members/nfc/" + nfcId, resp;
    int code = apiReq("GET", path, "", resp);
    if (code <= 0 || code == 401) { showUnknown(nfcId); return; }
    if (code == 404) {
        JsonDocument p; p["nfc_id"] = nfcId; String b; serializeJson(p, b);
        apiReq("POST", "/pending-nfc", b, resp); showUnknown(nfcId); return;
    }
    if (code != 200) return;
    JsonDocument doc; DeserializationError e = deserializeJson(doc, resp);
    if (e) return;
    bool sv = doc["safety_briefing_valid"] | false, ip = doc["is_present"] | false;
    String fn = doc["first_name"] | ""; if (fn == "") fn = doc["name"] | "";
    String sd = doc["last_safety_briefing"] | "keine";
    if (ip) {
        String ls = doc["current_login_at"] | ""; unsigned long session = 0;
        if (ls != "") { struct tm ltm = {0}; sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d", &ltm.tm_year, &ltm.tm_mon, &ltm.tm_mday, &ltm.tm_hour, &ltm.tm_min, &ltm.tm_sec);
            ltm.tm_year -= 1900; ltm.tm_mon -= 1; time_t lt = mktime(&ltm); time_t n; time(&n); session = difftime(n, lt); if (session < 0) session = 0; }
        JsonDocument co; co["nfc_id"] = nfcId; String b; serializeJson(co, b);
        apiReq("POST", "/members/check-out", b, resp); showCheckOut(fn, session);
    } else {
        JsonDocument ci; ci["nfc_id"] = nfcId; String b; serializeJson(ci, b);
        apiReq("POST", "/members/check-in", b, resp); showCheckIn(fn, sv, sd);
    }
}

void syncTime() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    time_t n; struct tm t; int r = 0;
    while (t.tm_year < (2024-1900) && r < 20) { delay(1000); time(&n); localtime_r(&n, &t); r++; }
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
}

void setup() {
    lv_begin();
    ui_init();

    Wire.begin(PIN_IIC_SDA, PIN_IIC_SCL);
    nfc.begin();
    uint32_t v = nfc.getFirmwareVersion();
    if (!v) Serial.println("PN532 not found!");
    else { Serial.printf("PN532 FW: %d.%d\n", (v>>24)&0xFF, (v>>16)&0xFF); nfc.SAMConfig(); }

    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) syncTime();
    Serial.println("Ready!");
}

void loop() {
    lv_handler();

    if ((screenState == SCREEN_CHECK_IN_OK || screenState == SCREEN_UNKNOWN) && millis() - screenStartMs > 10000) {
        screenState = SCREEN_IDLE; ui_init();
    }
    if (screenState == SCREEN_CHECK_OUT && millis() - screenStartMs > 15000) {
        screenState = SCREEN_IDLE; ui_init();
    }

    if (millis() < cardDebounce || millis() - lastCardCheck < 500) { delay(5); return; }
    lastCardCheck = millis();

    uint8_t uid[7] = {0}; uint8_t uidLen = 0;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 30)) {
        String nfcId = "";
        for (uint8_t i = 0; i < uidLen; i++) { if (uid[i] < 0x10) nfcId += "0"; nfcId += String(uid[i], HEX); }
        nfcId.toUpperCase(); Serial.printf("NFC: %s\n", nfcId.c_str());
        processCard(nfcId); cardDebounce = millis() + 2000;
    }
    delay(5);
}