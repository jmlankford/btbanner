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
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"
#include "ipixel.h"
#include "web.h"
#include "mqtt.h"
#include "playlist.h"

static void startWifi() {
  Preferences p;
  p.begin("ipixel", true);
  String ssid = p.getString("ssid", WIFI_STA_SSID);
  String pass = p.getString("pass", WIFI_STA_PASSWORD);
  p.end();

  WiFi.setHostname(WIFI_HOSTNAME);

  // Always run the AP *and* try to join the configured network (AP+STA). The
  // iPixelBridge hotspot at 192.168.4.1 therefore stays available for setup
  // even when the sign is also on your Wi-Fi, so you can never lose access to
  // the control page (and it sidesteps guest networks that isolate clients).
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_AP_SSID, strlen(WIFI_AP_PASSWORD) ? WIFI_AP_PASSWORD : nullptr);
  Serial.printf("[wifi] AP '%s' always on, http://%s/\n",
                WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

  if (ssid.length()) {
    Serial.printf("[wifi] joining '%s'\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_STA_TIMEOUT_MS)
      delay(250);
    if (WiFi.status() == WL_CONNECTED)
      Serial.printf("[wifi] also joined '%s', http://%s/\n",
                    ssid.c_str(), WiFi.localIP().toString().c_str());
    else
      Serial.println("[wifi] station join failed (AP still available)");
  }

  // mDNS: reach it at http://ipixel-bridge.local/ on networks that allow it
  // (many guest networks block mDNS, hence the always-on AP above).
  if (MDNS.begin(WIFI_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("[wifi] mDNS: http://%s.local/\n", WIFI_HOSTNAME);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ESP32 iPixel Color BLE bridge ===");

  startWifi();
  IPixel::begin();
  Playlist::begin();          // mount LittleFS, load saved content
  Web::begin();
  Mqtt::begin();              // no-op unless configured and on a Wi-Fi network

  if (BLE_AUTO_CONNECT) {
    Serial.println("[ble] auto-connect: scanning for a sign...");
    IPixel::connectFirst();   // non-fatal if nothing is found; retried in loop()
  }
}

void loop() {
  Web::loop();
  IPixel::loop();
  Mqtt::loop();
  Playlist::loop();
  delay(2);
}
