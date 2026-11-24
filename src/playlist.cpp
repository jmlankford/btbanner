// ---------------------------------------------------------------------------
// playlist.cpp  -  See playlist.h. Manifest in /playlist.json, image blobs in
// /img/<id>.bin, both on LittleFS.
// ---------------------------------------------------------------------------
#include "playlist.h"
#include "ipixel.h"
#include <LittleFS.h>
#include <vector>
#include <esp_heap_caps.h>

namespace Playlist {
namespace {

const char* MANIFEST = "/playlist.json";
const size_t MAX_ITEMS = 64;
const size_t KEEP_FREE = 8 * 1024;   // never fill the FS right to the edge

struct Item {
  String  id, name, type;            // type: "text" | "image"
  String  text;                      // text items
  uint8_t r = 255, g = 255, b = 255, anim = 1, speed = 80;
  bool    gif = false;               // image items
  uint16_t dwell = 8;                // seconds on screen during rotation
  bool    on = true;                 // included in rotation
};

std::vector<Item> g_items;
bool     g_rotate   = false;
int      g_cur      = -1;
uint32_t g_lastMs   = 0;

String imgPath(const String& id) { return "/img/" + id + ".bin"; }

String newId() {
  char b[9];
  snprintf(b, sizeof(b), "%06x", (unsigned)(esp_random() & 0xFFFFFF));
  return String(b);
}

void itemToJson(JsonObject o, const Item& it) {
  o["id"] = it.id; o["name"] = it.name; o["type"] = it.type;
  o["dwell"] = it.dwell; o["on"] = it.on;
  if (it.type == "text") {
    o["text"] = it.text;
    o["r"] = it.r; o["g"] = it.g; o["b"] = it.b;
    o["anim"] = it.anim; o["speed"] = it.speed;
  } else {
    o["gif"] = it.gif;
  }
}

void buildManifest(JsonDocument& d) {
  d["rotate"] = g_rotate;
  JsonArray arr = d["items"].to<JsonArray>();
  for (auto& it : g_items) itemToJson(arr.add<JsonObject>(), it);
}

void save() {
  JsonDocument d;
  buildManifest(d);
  File f = LittleFS.open(MANIFEST, "w");
  if (!f) { Serial.println("[play] save failed"); return; }
  serializeJson(d, f);
  f.close();
}

void load() {
  g_items.clear();
  File f = LittleFS.open(MANIFEST, "r");
  if (!f) return;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return; }
  f.close();
  g_rotate = d["rotate"] | false;
  for (JsonObject o : d["items"].as<JsonArray>()) {
    Item it;
    it.id = String((const char*)(o["id"] | ""));
    it.name = String((const char*)(o["name"] | "item"));
    it.type = String((const char*)(o["type"] | "text"));
    it.dwell = o["dwell"] | 8;
    it.on = o["on"] | true;
    if (it.type == "text") {
      it.text = String((const char*)(o["text"] | ""));
      it.r = o["r"] | 255; it.g = o["g"] | 255; it.b = o["b"] | 255;
      it.anim = o["anim"] | 1; it.speed = o["speed"] | 80;
    } else {
      it.gif = o["gif"] | false;
    }
    if (it.id.length()) g_items.push_back(it);
  }
}

Item* find(const String& id) {
  for (auto& it : g_items) if (it.id == id) return &it;
  return nullptr;
}

bool send(const Item& it) {
  if (it.type == "text")
    return IPixel::sendText(it.text, it.r, it.g, it.b, it.anim, it.speed);
  File f = LittleFS.open(imgPath(it.id), "r");
  if (!f) return false;
  size_t n = f.size();
  if (n == 0) { f.close(); return false; }
  // Load with a guarded allocation. The old std::vector<uint8_t> buf(n) threw
  // bad_alloc when the heap could not spare n contiguous bytes (BLE + MQTT-TLS
  // are live during a Play), and an uncaught throw aborts -> reboot on the
  // ESP32. heap_caps_malloc returns NULL instead, so we fail cleanly. Freed as
  // soon as the send completes, so it does not sit on the heap.
  uint8_t* buf = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_SPIRAM);  // PSRAM if any
  if (!buf) buf = (uint8_t*) heap_caps_malloc(n, MALLOC_CAP_8BIT);   // else DRAM
  if (!buf) { f.close(); Serial.printf("[play] out of memory for %u-byte image\n", (unsigned)n); return false; }
  size_t got = f.read(buf, n);
  f.close();
  bool r = (got == n) && IPixel::sendImage(it.gif, buf, got);
  free(buf);
  return r;
}

int nextEnabled(int from) {
  size_t n = g_items.size();
  for (size_t step = 1; step <= n; step++) {
    int i = (from + step) % n;
    if (g_items[i].on) return i;
  }
  return -1;
}

}  // namespace

void begin() {
  if (!LittleFS.begin(true)) { Serial.println("[play] LittleFS mount failed"); return; }
  if (!LittleFS.exists("/img")) LittleFS.mkdir("/img");
  load();
  Serial.printf("[play] %u items, rotate=%d, fs %u/%u bytes\n",
                (unsigned)g_items.size(), g_rotate,
                (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
}

void loop() {
  if (!g_rotate || g_items.empty() || !IPixel::isConnected()) return;
  uint16_t dwell = (g_cur >= 0 && g_cur < (int)g_items.size()) ? g_items[g_cur].dwell : 1;
  if (g_cur >= 0 && (millis() - g_lastMs) < (uint32_t)dwell * 1000) return;
  int nxt = nextEnabled(g_cur);
  if (nxt < 0) return;
  g_cur = nxt;
  g_lastMs = millis();
  send(g_items[g_cur]);
}

bool addText(const String& name, const String& text,
             uint8_t r, uint8_t g, uint8_t b, uint8_t anim, uint8_t speed,
             uint16_t dwell) {
  if (g_items.size() >= MAX_ITEMS) return false;
  Item it;
  it.id = newId(); it.name = name.length() ? name : "text";
  it.type = "text"; it.text = text;
  it.r = r; it.g = g; it.b = b; it.anim = anim; it.speed = speed;
  it.dwell = dwell ? dwell : 8;
  g_items.push_back(it);
  save();
  return true;
}

bool addImage(const String& name, bool gif, uint16_t dwell,
              const uint8_t* data, size_t len) {
  if (g_items.size() >= MAX_ITEMS || !data || !len) return false;
  if (LittleFS.usedBytes() + len + KEEP_FREE > LittleFS.totalBytes()) return false;  // full
  String id = newId();
  File f = LittleFS.open(imgPath(id), "w");
  if (!f) return false;
  size_t wrote = f.write(data, len);
  f.close();
  if (wrote != len) { LittleFS.remove(imgPath(id)); return false; }
  Item it;
  it.id = id; it.name = name.length() ? name : "image";
  it.type = "image"; it.gif = gif; it.dwell = dwell ? dwell : 10;
  g_items.push_back(it);
  save();
  return true;
}

bool updateItem(const String& id, const JsonVariantConst& f) {
  Item* it = find(id);
  if (!it) return false;
  if (f["name"].is<const char*>())  it->name  = String((const char*)f["name"]);
  if (f["dwell"].is<int>())         it->dwell = (uint16_t)f["dwell"];
  if (f["on"].is<bool>())           it->on    = f["on"];
  if (it->type == "text") {
    if (f["text"].is<const char*>()) it->text  = String((const char*)f["text"]);
    if (f["r"].is<int>())            it->r     = (uint8_t)f["r"];
    if (f["g"].is<int>())            it->g     = (uint8_t)f["g"];
    if (f["b"].is<int>())            it->b     = (uint8_t)f["b"];
    if (f["anim"].is<int>())         it->anim  = (uint8_t)f["anim"];
    if (f["speed"].is<int>())        it->speed = (uint8_t)f["speed"];
  }
  save();
  return true;
}

bool removeItem(const String& id) {
  for (size_t i = 0; i < g_items.size(); i++) {
    if (g_items[i].id == id) {
      if (g_items[i].type == "image") LittleFS.remove(imgPath(id));
      g_items.erase(g_items.begin() + i);
      if (g_cur >= (int)g_items.size()) g_cur = -1;
      save();
      return true;
    }
  }
  return false;
}

bool reorder(const JsonArrayConst& idOrder) {
  std::vector<Item> out;
  out.reserve(g_items.size());
  for (JsonVariantConst v : idOrder) {
    String id = String((const char*)(v | ""));
    for (size_t i = 0; i < g_items.size(); i++)
      if (g_items[i].id == id) { out.push_back(g_items[i]); g_items[i].id = ""; break; }
  }
  for (auto& it : g_items) if (it.id.length()) out.push_back(it);  // keep any not listed
  g_items = out;
  g_cur = -1;
  save();
  return true;
}

bool setRotate(bool on) {
  g_rotate = on;
  if (on) { g_cur = -1; g_lastMs = 0; }   // start the cycle promptly
  save();
  return true;
}

bool playNow(const String& id) {
  Item* it = find(id);
  if (!it) return false;
  for (size_t i = 0; i < g_items.size(); i++) if (&g_items[i] == it) g_cur = i;
  g_lastMs = millis();
  return send(*it);
}

void toJson(JsonDocument& out) {
  buildManifest(out);
  out["fs_used"]  = (uint32_t)LittleFS.usedBytes();
  out["fs_total"] = (uint32_t)LittleFS.totalBytes();
  out["count"]    = (uint32_t)g_items.size();
  out["max"]      = (uint32_t)MAX_ITEMS;
}

}  // namespace Playlist
