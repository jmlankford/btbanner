// ---------------------------------------------------------------------------
// main.cpp  -  Wi-Fi bring-up + wiring together the BLE and web modules.
//
// Boot sequence:
//   1. Load any Wi-Fi credentials saved via the web UI (NVS), else the
//      compile-time defaults in config.h.
//   2. Try to join that network (station mode). If it fails or none is set,
//      start the device's own access point (WIFI_AP_SSID).
//   3. Start the BLE stack and, if configured, auto-connect to the first sign.
//   4. Start the HTTP control server.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "ipixel.h"
#include "web.h"

static void startWifi() {
  Preferences p;
  p.begin("ipixel", true);
  String ssid = p.getString("ssid", WIFI_STA_SSID);
  String pass = p.getString("pass", WIFI_STA_PASSWORD);
  p.end();

  WiFi.setHostname(WIFI_HOSTNAME);

  if (ssid.length()) {
    Serial.printf("[wifi] joining '%s'\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_STA_TIMEOUT_MS)
      delay(250);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] connected, http://%s/\n", WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("[wifi] station join failed; falling back to AP");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, strlen(WIFI_AP_PASSWORD) ? WIFI_AP_PASSWORD : nullptr);
  Serial.printf("[wifi] AP '%s', http://%s/\n", WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 iPixel Color BLE bridge ===");

  startWifi();
  IPixel::begin();
  Web::begin();

  if (BLE_AUTO_CONNECT) {
    Serial.println("[ble] auto-connect: scanning for a sign...");
    IPixel::connectFirst();   // non-fatal if nothing is found; retried in loop()
  }
}

void loop() {
  Web::loop();
  IPixel::loop();
  delay(2);
}
