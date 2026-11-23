// ---------------------------------------------------------------------------
// playlist.h  -  On-device content library + auto-rotation.
//
// Stores named content items (scrolling text or an image/GIF) in LittleFS and
// can either play one on demand or cycle through the enabled ones on a timer.
// The manifest lives at /playlist.json; image bytes live at /img/<id>.bin.
// Survives reboots and is the same list seen from any browser.
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace Playlist {

void begin();          // mount LittleFS, load the manifest
void loop();           // advance the rotation when it is time

// Mutations (each persists the manifest and returns false on failure).
bool addText(const String& name, const String& text,
             uint8_t r, uint8_t g, uint8_t b, uint8_t anim, uint8_t speed,
             uint16_t dwell);
bool addImage(const String& name, bool gif, uint16_t dwell,
              const uint8_t* data, size_t len);
bool updateItem(const String& id, const JsonVariantConst& fields);
bool removeItem(const String& id);
bool reorder(const JsonArrayConst& idOrder);
bool setRotate(bool on);
bool playNow(const String& id);   // show this item immediately

// Render the manifest + storage stats as JSON into `out` (no image bytes).
void toJson(JsonDocument& out);

}  // namespace Playlist
