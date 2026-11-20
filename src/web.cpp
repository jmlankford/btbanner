// ---------------------------------------------------------------------------
// web.cpp  -  HTTP control server. Translates JSON requests into BLE commands.
// ---------------------------------------------------------------------------
#include "web.h"
#include "config.h"
#include "ipixel.h"
#include "index_html.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace Web {
namespace {

WebServer server(HTTP_PORT);

void sendJson(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

void ok(bool success, const char* err = nullptr) {
  JsonDocument d;
  d["ok"] = success;
  if (!success && err) d["error"] = err;
  sendJson(success ? 200 : 400, d);
}

// Parse the JSON request body; returns false (and replies 400) on bad JSON.
bool body(JsonDocument& d) {
  if (!server.hasArg("plain")) { ok(false, "missing body"); return false; }
  if (deserializeJson(d, server.arg("plain"))) { ok(false, "bad json"); return false; }
  return true;
}

bool requireBle() {
  if (IPixel::isConnected()) return true;
  ok(false, "sign not connected");
  return false;
}

// ---- handlers --------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  JsonDocument d;
  d["ok"]     = true;
  d["wifi"]   = WiFi.isConnected();
  d["ip"]     = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  d["ble"]    = IPixel::isConnected();
  d["device"] = IPixel::connectedName();
  d["notify"] = IPixel::lastNotifyHex();
  d["height"] = IPixel::reportedHeight();
  sendJson(200, d);
}

void handleScan() {
  auto results = IPixel::scan();
  JsonDocument d;
  d["ok"] = true;
  JsonArray arr = d["devices"].to<JsonArray>();
  for (auto& e : results) {
    JsonObject o = arr.add<JsonObject>();
    o["name"]    = e.name;
    o["address"] = e.address;
    o["type"]    = e.addrType;
  }
  sendJson(200, d);
}

void handleConnect() {
  JsonDocument d; if (!body(d)) return;
  const char* addr = d["address"] | "";
  uint8_t type = d["type"] | 0;
  if (!addr[0]) return ok(false, "missing address");
  ok(IPixel::connectTo(String(addr), type), "connect failed");
}

void handleDisconnect() { IPixel::disconnect(); ok(true); }

void handlePower()      { JsonDocument d; if (!body(d) || !requireBle()) return;
                          ok(IPixel::power(d["on"] | false)); }

void handleBrightness() { JsonDocument d; if (!body(d) || !requireBle()) return;
                          ok(IPixel::brightness((uint8_t)(d["level"] | 80))); }

void handleClear()      { if (!requireBle()) return; ok(IPixel::clear()); }

void handlePixel()      { JsonDocument d; if (!body(d) || !requireBle()) return;
                          ok(IPixel::setPixel(d["r"] | 255, d["g"] | 255, d["b"] | 255,
                                              d["x"] | 0, d["y"] | 0)); }

void handleText() {
  JsonDocument d; if (!body(d) || !requireBle()) return;
  const char* txt = d["text"] | "";
  ok(IPixel::sendText(String(txt),
                      d["r"] | 255, d["g"] | 255, d["b"] | 255,
                      (uint8_t)(d["anim"]  | TEXT_DEFAULT_ANIM),
                      (uint8_t)(d["speed"] | TEXT_DEFAULT_SPEED)));
}

void handleRaw() {
  JsonDocument d; if (!body(d) || !requireBle()) return;
  String hex = String((const char*)(d["hex"] | ""));
  std::vector<uint8_t> bytes;
  int hi = -1;
  for (size_t i = 0; i < hex.length(); i++) {
    char c = hex[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;                         // skip spaces / separators
    if (hi < 0) hi = v;
    else { bytes.push_back((uint8_t)((hi << 4) | v)); hi = -1; }
  }
  if (hi >= 0 || bytes.empty()) return ok(false, "need an even number of hex digits");
  ok(IPixel::sendRaw(bytes.data(), bytes.size()));
}

void handleWifi() {
  JsonDocument d; if (!body(d)) return;
  const char* ssid = d["ssid"] | "";
  const char* pass = d["pass"] | "";
  Preferences p;
  p.begin("ipixel", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  ok(true);
  delay(400);
  ESP.restart();
}

}  // namespace

void begin() {
  server.on("/",               HTTP_GET,  handleRoot);
  server.on("/api/status",     HTTP_GET,  handleStatus);
  server.on("/api/scan",       HTTP_GET,  handleScan);
  server.on("/api/connect",    HTTP_POST, handleConnect);
  server.on("/api/disconnect", HTTP_POST, handleDisconnect);
  server.on("/api/power",      HTTP_POST, handlePower);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.on("/api/clear",      HTTP_POST, handleClear);
  server.on("/api/pixel",      HTTP_POST, handlePixel);
  server.on("/api/text",       HTTP_POST, handleText);
  server.on("/api/raw",        HTTP_POST, handleRaw);
  server.on("/api/wifi",       HTTP_POST, handleWifi);
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
  Serial.printf("[web] listening on port %d\n", HTTP_PORT);
}

void loop() { server.handleClient(); }

}  // namespace Web
