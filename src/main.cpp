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

// Turn a free-form device name ("Jason-MonitorLED", "[LED] Sign") into a valid
// DNS hostname for setHostname()/mDNS: keep letters, digits and hyphens, map
// spaces/underscores to '-', trim leading '-', cap at 31 chars.
static String hostFromName(const String& name) {
  String o;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    if (isalnum((unsigned char)c) || c == '-') o += c;
    else if (c == ' ' || c == '_')             o += '-';
  }
  while (o.length() && o[0] == '-') o.remove(0, 1);
  if (!o.length())     o = "ipixel";
  if (o.length() > 31) o = o.substring(0, 31);
  return o;
}

static void startWifi() {
  Preferences p;
  p.begin("ipixel", true);
  String ssid    = p.getString("ssid", WIFI_STA_SSID);
  String pass    = p.getString("pass", WIFI_STA_PASSWORD);
  String devname = p.getString("devname", WIFI_AP_SSID);   // AP SSID + hostname
  p.end();

  String host = hostFromName(devname);
  WiFi.setHostname(host.c_str());

  // Always run the AP *and* try to join the configured network (AP+STA). The
  // hotspot (named <devname>) at 192.168.4.1 therefore stays available for
  // setup even when the sign is also on your Wi-Fi, so you can never lose
  // access (and it sidesteps guest networks that isolate clients).
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(devname.c_str(), strlen(WIFI_AP_PASSWORD) ? WIFI_AP_PASSWORD : nullptr);
  Serial.printf("[wifi] AP '%s' always on, http://%s/\n",
                devname.c_str(), WiFi.softAPIP().toString().c_str());

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
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("[wifi] mDNS: http://%s.local/\n", host.c_str());
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
