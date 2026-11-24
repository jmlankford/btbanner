// ---------------------------------------------------------------------------
// config.h  -  Build-time configuration for the ESP32 iPixel Color BLE bridge.
//
// Anything you might reasonably want to tweak without digging through the
// protocol code lives here: Wi-Fi behaviour, the BLE device filter, the GATT
// UUIDs, and the command opcodes. The opcodes are reverse-engineered (see
// README.md for provenance) and may differ on your specific sign's firmware;
// if a command misbehaves, this is the first file to edit.
// ---------------------------------------------------------------------------
#pragma once

// ---- Wi-Fi -----------------------------------------------------------------
// Leave WIFI_STA_SSID empty to boot straight into access-point mode. If set,
// the bridge first tries to join this network and falls back to the AP after
// WIFI_STA_TIMEOUT_MS. Credentials entered through the web UI are stored in
// NVS and take precedence over these compile-time defaults.
#define WIFI_STA_SSID        ""
#define WIFI_STA_PASSWORD    ""
#define WIFI_STA_TIMEOUT_MS  15000

// Access-point fallback (the bridge's own hotspot).
#define WIFI_AP_SSID         "iPixelBridge"
#define WIFI_AP_PASSWORD     "pixel1234"   // >= 8 chars, or "" for an open AP
#define WIFI_HOSTNAME        "ipixel-bridge"

#define HTTP_PORT            80

// ---- BLE device discovery --------------------------------------------------
// iPixel Color signs advertise with this name prefix. Scan results are
// filtered to names starting with it; set to "" to list every BLE device.
#define BLE_NAME_PREFIX      "LED_BLE"
#define BLE_SCAN_SECONDS     5
#define BLE_AUTO_CONNECT     true   // connect to the first match automatically

// ---- GATT layout -----------------------------------------------------------
// Characteristics are stable across the whole product family. The service UUID
// is not documented, so the firmware discovers the service that contains the
// write characteristic; this value is only a first guess.
#define BLE_SERVICE_UUID_GUESS  "fa00"
#define BLE_WRITE_UUID          "fa02"   // Write / Write-Without-Response
#define BLE_NOTIFY_UUID         "fa03"   // Notify (command ACKs, device info)

// Negotiated ATT MTU. The text transport writes in 244-byte GATT chunks, so we
// ask for an MTU large enough to carry them in a single write.
#define BLE_MTU                 247
// How long to wait for a per-window notify ACK. This sign does not send ACKs
// at all, so the wait is pure dead time that (because the web server shares
// this single thread) freezes the UI during every send. The GATT writes are
// already acknowledged at the link layer, so the data is delivered regardless;
// this is just a short settle so the sign can render before the next window.
// Kept short to keep the server responsive. A sign that DOES ACK still exits
// the wait early the moment its notify arrives.
#define BLE_ACK_TIMEOUT_MS      800

// ---- iPixel command opcodes ------------------------------------------------
// Simple commands are framed as: [len, 0x00, cmdId, typeId, payload...]
// where `len` is the total command length in bytes. These bytes are ported
// verbatim from the pypixelcolor reference implementation.
//
//   Power      : { 5, 0, 7, 1, on }
//   Brightness : { 5, 0, 4, 0x80, level }        level 0..100
//   Clear      : { 4, 0, 3, 0x80 }               erases stored content
//   Fun mode   : { 5, 0, 4, 1, enable }
//   Set pixel  : { 10, 0, 5, 1, 0, R, G, B, x, y }
//   Device info: { 8, 0, 1, 0x80, hh, mm, ss, 0 }  (also sets the clock)
//
// They are built inline in ipixel.cpp; the constants below are the cmd/type
// IDs, kept here so a per-device correction is a one-line change.
#define OP_POWER_CMD      0x07
#define OP_POWER_TYPE     0x01
#define OP_BRIGHT_CMD     0x04
#define OP_BRIGHT_TYPE    0x80
#define OP_CLEAR_CMD      0x03
#define OP_CLEAR_TYPE     0x80
#define OP_FUN_CMD        0x04
#define OP_FUN_TYPE       0x01
#define OP_PIXEL_CMD      0x05
#define OP_PIXEL_TYPE     0x01
#define OP_DEVINFO_CMD    0x01
#define OP_DEVINFO_TYPE   0x80

// ---- Text rendering defaults -----------------------------------------------
// Most flexible signs in this family are 16 px tall; the embedded font is 8x16.
// If device-info reports a height we use that, otherwise we assume this.
#define TEXT_DEFAULT_HEIGHT   16
#define TEXT_DEFAULT_SPEED    80     // 0..100
#define TEXT_DEFAULT_ANIM     1      // 1 = scroll left (see TextAnim in ipixel.h)
#define TEXT_MAX_CHARS        255    // num_chars is a single byte
#define TEXT_WINDOW_BYTES     (12 * 1024)
#define BLE_CHUNK_BYTES       244

// ---- MQTT (optional remote control) ----------------------------------------
// When enabled and the bridge is on a Wi-Fi network with internet, it connects
// OUTBOUND to an MQTT broker over TLS and subscribes to <base>/cmd. Publish a
// message there (Home Assistant, Apple Shortcuts, Tasker, ...) and the sign
// updates from anywhere. Nothing is exposed inbound, so it works behind guest
// Wi-Fi with client isolation. Set these from the web UI (stored in NVS); the
// values here are only fallback defaults. Keep secrets OUT of this file.
#define MQTT_HOST_DEFAULT        ""     // e.g. "abcd1234.s1.eu.hivemq.cloud"
#define MQTT_DEFAULT_PORT        8883   // 8883 = MQTT over TLS
#define MQTT_BASE_TOPIC_DEFAULT  "ipixel"
