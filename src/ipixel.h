// ---------------------------------------------------------------------------
// ipixel.h  -  BLE-central control of an iPixel Color flexible LED sign.
//
// The ESP32 acts as the phone app would: it scans for a `LED_BLE_*` peripheral,
// connects, discovers the write/notify characteristics, and sends framed
// commands. All device-facing protocol lives behind this interface so the web
// layer never touches raw bytes.
//
// IMPORTANT: the sign accepts only ONE BLE connection at a time. While this
// bridge is connected the official iPixel Color app cannot connect, and vice
// versa.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>
#include <vector>

namespace IPixel {

// Text animation modes (TextAnimation enum, from the reference implementation).
enum TextAnim : uint8_t {
  ANIM_STATIC       = 0,
  ANIM_SCROLL_LEFT  = 1,
  ANIM_SCROLL_RIGHT = 2,
  ANIM_SCROLL_UP    = 3,
  ANIM_SCROLL_DOWN  = 4,
  ANIM_BLINK        = 5,
  ANIM_FADE         = 6,
  ANIM_SNOWFLAKE    = 7,
};

struct ScanEntry {
  String name;
  String address;
  uint8_t addrType;
};

// Lifecycle ------------------------------------------------------------------
void begin();        // init the BLE stack; call once from setup()
void loop();         // housekeeping + auto-reconnect; call every loop()

// Discovery / connection -----------------------------------------------------
std::vector<ScanEntry> scan();          // blocking scan (BLE_SCAN_SECONDS)
bool connectTo(const String& address, uint8_t addrType);
bool connectFirst();                    // connect to first matching peripheral
void disconnect();
bool isConnected();
String connectedName();
String lastNotifyHex();                 // most recent notify payload, as hex
int reportedHeight();                   // sign height in px (device-info or default)

// Commands -------------------------------------------------------------------
// Each returns true if the GATT write was issued on a live connection.
bool power(bool on);
bool brightness(uint8_t level);         // 0..100
bool clear();
bool funMode(bool enable);
bool setPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t x, uint8_t y);
bool sendText(const String& text, uint8_t r, uint8_t g, uint8_t b,
              uint8_t anim, uint8_t speed);

// Upload an image the device decodes itself. `data` is the raw file bytes,
// already sized to the panel (PNG for static, GIF for animated).
bool sendImage(bool isGif, const uint8_t* data, size_t len, uint8_t saveSlot = 0);

// Escape hatch for reverse-engineering: send a raw, fully-formed frame.
bool sendRaw(const uint8_t* data, size_t len);

}  // namespace IPixel
