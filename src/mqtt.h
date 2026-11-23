// ---------------------------------------------------------------------------
// mqtt.h  -  Optional outbound MQTT client for remote control.
//
// When enabled (and the bridge is on a Wi-Fi network with internet), the ESP32
// connects OUTBOUND to an MQTT broker over TLS and subscribes to <base>/cmd.
// Anything published there (from Home Assistant, Apple Shortcuts, Tasker, ...)
// drives the sign. Nothing is exposed inbound, so this works behind guest
// networks with client isolation and no port-forwarding.
//
// Command topic  <base>/cmd   payload is JSON, e.g.
//   {"type":"text","text":"Hello","r":255,"g":128,"b":0,"anim":1,"speed":80}
//   {"type":"power","on":true}
//   {"type":"brightness","level":60}
//   {"type":"clear"}
//   {"type":"raw","hex":"05 00 07 01 01"}
// A non-JSON payload is treated as scrolling text with defaults, so publishing
// the bare string "Back at 6" just works.
//
// Status topic   <base>/status  retained "online"/"offline" (offline via LWT)
// Ack topic      <base>/ack     {"ok":true,"cmd":"text"} after each command
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

namespace Mqtt {
void begin();          // read config from NVS and set up the client
void loop();           // service + reconnect; call every loop()
bool enabled();        // configured and turned on
bool connected();      // currently connected to the broker
String statusText();   // short human string for the web UI
}
