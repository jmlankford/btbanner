// ---------------------------------------------------------------------------
// web.cpp  -  HTTP control server. Translates JSON requests into BLE commands.
// ---------------------------------------------------------------------------
#include "web.h"
#include "config.h"
#include "ipixel.h"
#include "mqtt.h"
#include "playlist.h"
#include "index_html.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_heap_caps.h>

namespace Web {
namespace {

WebServer server(HTTP_PORT);

size_t imageMaxBytes();   // defined below; the real image/GIF size limit in bytes

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
  d["mqtt"]   = Mqtt::statusText();
  d["imgMax"] = (uint32_t)imageMaxBytes();   // real image/GIF byte limit
  d["psram"]  = (uint32_t)ESP.getPsramSize();
  {
    Preferences p;
    p.begin("ipixel", true);
    d["name"] = p.getString("devname", WIFI_AP_SSID);
    p.end();
  }
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
  const char* name = d["name"] | "";
  Preferences p;
  p.begin("ipixel", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  if (strlen(name)) p.putString("devname", name);   // only when provided
  p.end();
  ok(true);
  delay(400);
  ESP.restart();
}

void handleMqtt() {
  JsonDocument d; if (!body(d)) return;
  Preferences p;
  p.begin("ipixel", false);
  p.putString("mq_host", (const char*)(d["host"] | ""));
  p.putInt   ("mq_port", (int)(d["port"] | MQTT_DEFAULT_PORT));
  p.putString("mq_user", (const char*)(d["user"] | ""));
  p.putString("mq_pass", (const char*)(d["pass"] | ""));
  p.putString("mq_base", (const char*)(d["base"] | MQTT_BASE_TOPIC_DEFAULT));
  p.putBool  ("mq_en",   d["enable"] | false);
  p.end();
  ok(true);
  delay(400);
  ESP.restart();   // reconnect cleanly with the new broker settings
}

// ---- image upload ----------------------------------------------------------
// The browser resizes/encodes to the panel size and POSTs the file bytes as
// multipart/form-data to /api/image?gif=0|1. We buffer the bytes and forward
// them to the sign, which decodes the PNG/GIF itself.
// The image/GIF buffer is a single raw block, allocated ONCE at boot and reused
// for every upload, so the upload path never allocates and can never OOM-reboot
// mid-transfer (the old std::vector grew by doubling, and a failed realloc threw
// bad_alloc -> abort). heap_caps_malloc returns NULL on failure instead of
// throwing, so even the one boot allocation is safe.
//
// If the module has PSRAM (WROVER) the buffer goes there and can be large; on a
// no-PSRAM board (WROOM) it falls back to a modest DRAM block, sized so it does
// not starve MQTT-over-TLS. g_imgCap is the real maximum and is reported to the
// web UI so the shown limit always matches reality.
uint8_t* g_img    = nullptr;
size_t   g_imgCap = 0;             // allocated capacity == the real max image size
size_t   g_imgLen = 0;             // bytes received for the current upload
bool     g_imgTooBig = false;

static const size_t IMG_MAX_PSRAM = 512 * 1024;   // generous when PSRAM exists
static const size_t IMG_MAX_DRAM  = 48  * 1024;   // safe no-PSRAM ceiling

size_t imageMaxBytes() { return g_imgCap; }

void imgBufInit() {
  if (psramFound()) {
    g_imgCap = IMG_MAX_PSRAM;
    g_img = (uint8_t*) heap_caps_malloc(g_imgCap, MALLOC_CAP_SPIRAM);
  }
  if (!g_img) {                     // no PSRAM, or the PSRAM alloc failed
    g_imgCap = IMG_MAX_DRAM;
    if (ESP.getMaxAllocHeap() >= g_imgCap + 8192)
      g_img = (uint8_t*) heap_caps_malloc(g_imgCap, MALLOC_CAP_8BIT);
    if (!g_img) g_imgCap = 0;       // give up gracefully; handlers report it
  }
  Serial.printf("[web] image buffer: %u bytes in %s (psram total %u)\n",
                (unsigned)g_imgCap,
                (g_img && psramFound()) ? "PSRAM" : "DRAM",
                (unsigned)ESP.getPsramSize());
}

void handleImageUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_imgLen = 0;
    g_imgTooBig = false;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!g_img) return;             // no buffer (boot allocation failed)
    if (g_imgLen + up.currentSize > g_imgCap) { g_imgTooBig = true; return; }
    memcpy(g_img + g_imgLen, up.buf, up.currentSize);
    g_imgLen += up.currentSize;
  }
}

void handleImageDone() {
  if (!IPixel::isConnected()) { g_imgLen = 0; return ok(false, "sign not connected"); }
  if (!g_img)      { g_imgLen = 0; return ok(false, "no image buffer (low memory at boot)"); }
  if (g_imgTooBig) { g_imgLen = 0; String e = String("image too large (max ") + (g_imgCap / 1024) + " KB)"; return ok(false, e.c_str()); }
  if (g_imgLen == 0) return ok(false, "no image data");
  bool gif = (server.arg("gif") == "1");
  bool r = IPixel::sendImage(gif, g_img, g_imgLen);
  g_imgLen = 0;
  ok(r);
}

// ---- over-the-air firmware update ------------------------------------------
// Upload the app image (firmware-ota.bin, NOT the merged bin) to /update and
// the ESP32 writes it to the spare OTA slot and reboots into it. This is how
// every flash after the first serial one is done, straight from this page.
void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[ota] done: %u bytes\n", up.totalSize);
    else Update.printError(Serial);
  }
}

void handleUpdateDone() {
  bool good = !Update.hasError();
  JsonDocument d;
  d["ok"] = good;
  if (!good) d["error"] = String(Update.errorString());
  sendJson(good ? 200 : 400, d);
  if (good) { delay(500); ESP.restart(); }
}

// ---- content library / playlist --------------------------------------------
void handlePlaylistGet() {
  JsonDocument d;
  Playlist::toJson(d);
  d["ok"] = true;
  sendJson(200, d);
}

void handlePlaylistAddText() {
  JsonDocument d; if (!body(d)) return;
  bool r = Playlist::addText(String((const char*)(d["name"] | "")),
                             String((const char*)(d["text"] | "")),
                             d["r"] | 255, d["g"] | 255, d["b"] | 255,
                             (uint8_t)(d["anim"] | 1), (uint8_t)(d["speed"] | 80),
                             (uint16_t)(d["dwell"] | 8));
  ok(r, "add failed (library full?)");
}

void handlePlaylistImageDone() {
  if (!g_img)      { g_imgLen = 0; return ok(false, "no image buffer (low memory at boot)"); }
  if (g_imgTooBig) { g_imgLen = 0; String e = String("image too large (max ") + (g_imgCap / 1024) + " KB)"; return ok(false, e.c_str()); }
  if (g_imgLen == 0) return ok(false, "no image data");
  bool gif = (server.arg("gif") == "1");
  bool r = Playlist::addImage(server.arg("name"), gif,
                              (uint16_t)server.arg("dwell").toInt(),
                              g_img, g_imgLen);
  g_imgLen = 0;
  ok(r, "add failed (storage full or library full?)");
}

void handlePlaylistUpdate() {
  JsonDocument d; if (!body(d)) return;
  String id = String((const char*)(d["id"] | ""));
  if (!id.length()) return ok(false, "missing id");
  ok(Playlist::updateItem(id, d.as<JsonVariantConst>()), "no such item");
}

void handlePlaylistDelete() {
  JsonDocument d; if (!body(d)) return;
  ok(Playlist::removeItem(String((const char*)(d["id"] | ""))), "no such item");
}

void handlePlaylistReorder() {
  JsonDocument d; if (!body(d)) return;
  ok(Playlist::reorder(d["order"].as<JsonArrayConst>()));
}

void handlePlaylistPlay() {
  JsonDocument d; if (!body(d) || !requireBle()) return;
  ok(Playlist::playNow(String((const char*)(d["id"] | ""))), "play failed");
}

void handlePlaylistRotate() {
  JsonDocument d; if (!body(d)) return;
  ok(Playlist::setRotate(d["on"] | false));
}

}  // namespace

void begin() {
  imgBufInit();   // grab the image buffer now, while the heap is clean
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
  server.on("/api/mqtt",       HTTP_POST, handleMqtt);
  server.on("/api/image",      HTTP_POST, handleImageDone, handleImageUpload);
  server.on("/update",         HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/api/playlist",         HTTP_GET,  handlePlaylistGet);
  server.on("/api/playlist/text",    HTTP_POST, handlePlaylistAddText);
  server.on("/api/playlist/image",   HTTP_POST, handlePlaylistImageDone, handleImageUpload);
  server.on("/api/playlist/update",  HTTP_POST, handlePlaylistUpdate);
  server.on("/api/playlist/delete",  HTTP_POST, handlePlaylistDelete);
  server.on("/api/playlist/reorder", HTTP_POST, handlePlaylistReorder);
  server.on("/api/playlist/play",    HTTP_POST, handlePlaylistPlay);
  server.on("/api/playlist/rotate",  HTTP_POST, handlePlaylistRotate);
  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
  Serial.printf("[web] listening on port %d\n", HTTP_PORT);
}

void loop() { server.handleClient(); }

}  // namespace Web
