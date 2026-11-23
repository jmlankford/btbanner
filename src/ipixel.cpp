// ---------------------------------------------------------------------------
// ipixel.cpp  -  Implementation of the iPixel Color BLE-central protocol.
//
// Protocol provenance (see README.md): ported from the community reverse-
// engineering projects pypixelcolor, go-ipxl and ha-ipixel-color. Byte layouts
// were read from source, not captured from a device, so treat them as a strong
// hypothesis and verify against your hardware with /api/raw if needed.
// ---------------------------------------------------------------------------
#include "ipixel.h"
#include "config.h"
#include "font8x16.h"
#include <NimBLEDevice.h>

namespace IPixel {
namespace {

NimBLEClient*                client      = nullptr;
NimBLERemoteCharacteristic*  writeChar   = nullptr;
NimBLERemoteCharacteristic*  notifyChar  = nullptr;

volatile bool  g_connected   = false;
volatile bool  g_ack         = false;
String         g_name;
String         g_lastNotify;
int            g_height      = TEXT_DEFAULT_HEIGHT;

// Reconnect bookkeeping.
bool           g_want        = false;      // we have a target we want connected
String         g_addr;
uint8_t        g_addrType    = 0;
uint32_t       g_nextRetry   = 0;

// Scan results, filled by the advertised-device callback.
std::vector<ScanEntry> g_scanResults;

// ---- small helpers ---------------------------------------------------------
uint32_t crc32(const uint8_t* d, size_t n) {   // zlib / IEEE CRC-32
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++)
      c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
  }
  return c ^ 0xFFFFFFFFu;
}

void putLE16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
}
void putLE32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
  v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
}

String toHex(const uint8_t* d, size_t n) {
  static const char* H = "0123456789ABCDEF";
  String s; s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += H[d[i] >> 4]; s += H[d[i] & 0xF]; }
  return s;
}

// ---- BLE callbacks ---------------------------------------------------------
class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* /*c*/) override {
    g_connected = false;
    writeChar = notifyChar = nullptr;
    Serial.println("[ble] disconnected");
  }
};
ClientCB g_clientCb;

class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    String name = dev->haveName() ? String(dev->getName().c_str()) : String();
    const char* prefix = BLE_NAME_PREFIX;
    if (prefix[0] && !name.startsWith(prefix)) return;
    ScanEntry e;
    e.name     = name.length() ? name : String("(unnamed)");
    e.address  = String(dev->getAddress().toString().c_str());
    e.addrType = dev->getAddress().getType();
    for (auto& r : g_scanResults) if (r.address == e.address) return;  // dedupe
    g_scanResults.push_back(e);
  }
};
ScanCB g_scanCb;

void notifyCB(NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t len, bool /*isNotify*/) {
  g_ack = true;
  g_lastNotify = toHex(data, len);
  // Best-effort device-info parse: the response layout is undocumented, so we
  // only record the raw bytes. Height stays at the configured default; adjust
  // here once you have confirmed the layout for your sign.
}

// ---- low-level writes ------------------------------------------------------
bool writeRaw(const uint8_t* data, size_t len) {
  if (!g_connected || !writeChar) return false;
  return writeChar->writeValue(const_cast<uint8_t*>(data), len, true);
}

bool writeChunked(const std::vector<uint8_t>& pkt) {
  if (!g_connected || !writeChar) return false;
  size_t off = 0;
  while (off < pkt.size()) {
    size_t take = min((size_t)BLE_CHUNK_BYTES, pkt.size() - off);
    if (!writeChar->writeValue(const_cast<uint8_t*>(pkt.data()) + off, take, true))
      return false;
    off += take;
  }
  return true;
}

// Wrap a data payload in the windowed/CRC transport used for text & images and
// send it. Windows are 12 KiB; each window carries a 13-byte header (its first
// two bytes and the tail byte identify the payload type) and the CRC of the
// *entire* payload.
//   text: b0=0x00 b1=0x01 tail=0x00 | PNG: 0x02/0x00/0x00 | GIF: 0x03/0x00/0x02
bool sendFramed(const std::vector<uint8_t>& payload,
                uint8_t b0, uint8_t b1, uint8_t tail, uint8_t saveSlot) {
  if (!g_connected || !writeChar) return false;
  const uint32_t crc  = crc32(payload.data(), payload.size());
  const uint32_t psz  = payload.size();
  size_t off = 0;
  bool first = true;
  while (off < payload.size() || first) {
    size_t take = min((size_t)TEXT_WINDOW_BYTES, payload.size() - off);
    std::vector<uint8_t> frame;
    frame.push_back(b0);
    frame.push_back(b1);
    frame.push_back(first ? 0x00 : 0x02);          // option
    putLE32(frame, psz);                           // total payload size
    putLE32(frame, crc);                           // CRC of whole payload
    frame.push_back(tail);
    frame.push_back(saveSlot);
    frame.insert(frame.end(), payload.begin() + off, payload.begin() + off + take);

    std::vector<uint8_t> pkt;
    putLE16(pkt, (uint16_t)(frame.size() + 2));    // prefix counts itself
    pkt.insert(pkt.end(), frame.begin(), frame.end());

    g_ack = false;
    if (!writeChunked(pkt)) return false;

    uint32_t t0 = millis();                        // wait (softly) for the ACK
    while (!g_ack && (millis() - t0) < BLE_ACK_TIMEOUT_MS) delay(10);
    if (!g_ack) Serial.println("[ble] warning: no ACK for window");

    off += take;
    first = false;
  }
  return true;
}

void discoverChars() {
  writeChar = notifyChar = nullptr;
  // Try the guessed service first, then fall back to scanning every service
  // for the write characteristic (the service UUID is not documented).
  NimBLERemoteService* svc = client->getService(NimBLEUUID(BLE_SERVICE_UUID_GUESS));
  if (svc) writeChar = svc->getCharacteristic(NimBLEUUID(BLE_WRITE_UUID));
  if (!writeChar) {
    std::vector<NimBLERemoteService*>* all = client->getServices(true);
    for (auto* s : *all) {
      NimBLERemoteCharacteristic* w = s->getCharacteristic(NimBLEUUID(BLE_WRITE_UUID));
      if (w) { svc = s; writeChar = w; break; }
    }
  }
  if (svc) notifyChar = svc->getCharacteristic(NimBLEUUID(BLE_NOTIFY_UUID));
}

bool doConnect(const String& addr, uint8_t type) {
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&g_clientCb, false);
  }
  if (client->isConnected()) client->disconnect();

  Serial.printf("[ble] connecting to %s\n", addr.c_str());
  if (!client->connect(NimBLEAddress(std::string(addr.c_str()), type), true)) {
    Serial.println("[ble] connect failed");
    return false;
  }
  client->updateConnParams(12, 24, 0, 400);
  discoverChars();
  if (!writeChar) {
    Serial.println("[ble] write characteristic not found; disconnecting");
    client->disconnect();
    return false;
  }
  if (notifyChar && notifyChar->canNotify())
    notifyChar->subscribe(true, notifyCB);

  g_connected = true;
  g_name = addr;
  g_addr = addr;
  g_addrType = type;
  g_want = true;
  Serial.println("[ble] connected & ready");

  // Post-connect handshake: send the device-info / time request. Its response
  // arrives on the notify characteristic (recorded by notifyCB).
  const uint8_t info[] = { 8, 0, OP_DEVINFO_CMD, OP_DEVINFO_TYPE, 0, 0, 0, 0 };
  writeRaw(info, sizeof(info));
  return true;
}

}  // namespace

// ---- public API ------------------------------------------------------------
void begin() {
  NimBLEDevice::init(WIFI_HOSTNAME);
  NimBLEDevice::setMTU(BLE_MTU);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  Serial.println("[ble] stack initialised");
}

void loop() {
  if (g_want && !g_connected && millis() > g_nextRetry) {
    g_nextRetry = millis() + 5000;
    doConnect(g_addr, g_addrType);
  }
}

std::vector<ScanEntry> scan() {
  g_scanResults.clear();
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setAdvertisedDeviceCallbacks(&g_scanCb, false);
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(99);
  s->start(BLE_SCAN_SECONDS, false);
  s->clearResults();
  return g_scanResults;
}

bool connectTo(const String& address, uint8_t addrType) {
  return doConnect(address, addrType);
}

bool connectFirst() {
  auto results = scan();
  if (results.empty()) return false;
  return doConnect(results[0].address, results[0].addrType);
}

void disconnect() {
  g_want = false;
  if (client && client->isConnected()) client->disconnect();
  g_connected = false;
}

bool   isConnected()    { return g_connected; }
String connectedName()  { return g_connected ? g_name : String(); }
String lastNotifyHex()  { return g_lastNotify; }
int    reportedHeight() { return g_height; }

bool power(bool on) {
  const uint8_t c[] = { 5, 0, OP_POWER_CMD, OP_POWER_TYPE, (uint8_t)(on ? 1 : 0) };
  return writeRaw(c, sizeof(c));
}

bool brightness(uint8_t level) {
  if (level > 100) level = 100;
  const uint8_t c[] = { 5, 0, OP_BRIGHT_CMD, OP_BRIGHT_TYPE, level };
  return writeRaw(c, sizeof(c));
}

bool clear() {
  const uint8_t c[] = { 4, 0, OP_CLEAR_CMD, OP_CLEAR_TYPE };
  return writeRaw(c, sizeof(c));
}

bool funMode(bool enable) {
  const uint8_t c[] = { 5, 0, OP_FUN_CMD, OP_FUN_TYPE, (uint8_t)(enable ? 1 : 0) };
  return writeRaw(c, sizeof(c));
}

bool setPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t x, uint8_t y) {
  const uint8_t c[] = { 10, 0, OP_PIXEL_CMD, OP_PIXEL_TYPE, 0, r, g, b, x, y };
  return writeRaw(c, sizeof(c));
}

bool sendText(const String& text, uint8_t r, uint8_t g, uint8_t b,
              uint8_t anim, uint8_t speed) {
  if (!g_connected || !writeChar) return false;
  if (speed > 100) speed = 100;

  int n = text.length();
  if (n > TEXT_MAX_CHARS) n = TEXT_MAX_CHARS;

  std::vector<uint8_t> payload;
  payload.push_back((uint8_t)n);

  // 13-byte properties block: marker(3) anim speed rainbow color(3) bg(4)
  payload.push_back(0x00); payload.push_back(0x01); payload.push_back(0x01);
  payload.push_back(anim);
  payload.push_back(speed);
  payload.push_back(0x00);               // rainbow mode off
  payload.push_back(r); payload.push_back(g); payload.push_back(b);
  payload.push_back(0x00); payload.push_back(0x00);
  payload.push_back(0x00); payload.push_back(0x00);   // no background colour

  // Per-character blocks: [opcode][R][G][B][glyph rows...]
  // opcode 0x00 = height < 32, half-width (8 px) -> one byte per row.
  for (int i = 0; i < n; i++) {
    char ch = text[i];
    if (ch < FONT_FIRST_CHAR || ch > FONT_LAST_CHAR) ch = '?';
    const uint8_t* glyph = FONT8X16[ch - FONT_FIRST_CHAR];
    payload.push_back(0x00);
    payload.push_back(r); payload.push_back(g); payload.push_back(b);
    for (int row = 0; row < FONT_HEIGHT; row++)
      payload.push_back(pgm_read_byte(&glyph[row]));
  }

  return sendFramed(payload, 0x00, 0x01, 0x00, 0);
}

bool sendImage(bool isGif, const uint8_t* data, size_t len, uint8_t saveSlot) {
  if (!g_connected || !writeChar || !data || !len) return false;
  std::vector<uint8_t> payload(data, data + len);
  // The device decodes the PNG/GIF itself; we just stream the raw file bytes
  // through the same windowed/CRC transport with the image type bytes.
  //   PNG: b0=0x02 tail=0x00   GIF: b0=0x03 tail=0x02
  return sendFramed(payload, isGif ? 0x03 : 0x02, 0x00, isGif ? 0x02 : 0x00, saveSlot);
}

bool sendRaw(const uint8_t* data, size_t len) {
  return writeRaw(data, len);
}

}  // namespace IPixel
