// ---------------------------------------------------------------------------
// mqtt.cpp  -  Outbound MQTT-over-TLS control. See mqtt.h for the topic/payload
// contract. Config is stored in NVS (namespace "ipixel", keys mq_*) and set
// from the web UI.
// ---------------------------------------------------------------------------
#include "mqtt.h"
#include "config.h"
#include "ipixel.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

namespace Mqtt {
namespace {

WiFiClientSecure net;
PubSubClient     client(net);

String   g_host, g_user, g_pass, g_base, g_clientId;
uint16_t g_port    = MQTT_DEFAULT_PORT;
bool     g_enabled = false;
uint32_t g_nextTry = 0;

String topic(const char* leaf) { return g_base + "/" + leaf; }

// Parse a loose hex string ("05 00 07 01 01") into bytes. Returns false if it
// has an odd number of hex digits or none at all.
bool parseHex(const String& hex, std::vector<uint8_t>& out) {
  int hi = -1;
  for (size_t i = 0; i < hex.length(); i++) {
    char c = hex[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;
    if (hi < 0) hi = v;
    else { out.push_back((uint8_t)((hi << 4) | v)); hi = -1; }
  }
  return hi < 0 && !out.empty();
}

void onMessage(char* /*t*/, uint8_t* payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];

  bool   ok  = false;
  String cmd = "text";

  JsonDocument d;
  bool isJson = (deserializeJson(d, msg) == DeserializationError::Ok) && d.is<JsonObject>();

  if (isJson) {
    cmd = String((const char*)(d["type"] | "text"));
    if (cmd == "text") {
      ok = IPixel::sendText(String((const char*)(d["text"] | "")),
                            d["r"] | 255, d["g"] | 255, d["b"] | 255,
                            (uint8_t)(d["anim"]  | TEXT_DEFAULT_ANIM),
                            (uint8_t)(d["speed"] | TEXT_DEFAULT_SPEED));
    } else if (cmd == "power") {
      ok = IPixel::power(d["on"] | false);
    } else if (cmd == "brightness") {
      ok = IPixel::brightness((uint8_t)(d["level"] | 80));
    } else if (cmd == "clear") {
      ok = IPixel::clear();
    } else if (cmd == "raw") {
      std::vector<uint8_t> bytes;
      if (parseHex(String((const char*)(d["hex"] | "")), bytes))
        ok = IPixel::sendRaw(bytes.data(), bytes.size());
    }
  } else {
    // Bare string -> scrolling text with defaults (white, scroll-left).
    ok  = IPixel::sendText(msg, 255, 255, 255, TEXT_DEFAULT_ANIM, TEXT_DEFAULT_SPEED);
    cmd = "text";
  }

  String ack = String("{\"ok\":") + (ok ? "true" : "false") + ",\"cmd\":\"" + cmd + "\"}";
  client.publish(topic("ack").c_str(), ack.c_str(), false);
  Serial.printf("[mqtt] cmd=%s ok=%d\n", cmd.c_str(), ok);
}

bool connect() {
  String willTopic = topic("status");
  bool r = client.connect(g_clientId.c_str(),
                          g_user.length() ? g_user.c_str() : nullptr,
                          g_pass.length() ? g_pass.c_str() : nullptr,
                          willTopic.c_str(), 0, true, "offline");
  if (r) {
    client.publish(willTopic.c_str(), "online", true);
    client.subscribe(topic("cmd").c_str());
    Serial.printf("[mqtt] connected, subscribed %s\n", topic("cmd").c_str());
  } else {
    Serial.printf("[mqtt] connect failed, state=%d\n", client.state());
  }
  return r;
}

}  // namespace

void begin() {
  Preferences p;
  p.begin("ipixel", true);
  g_host = p.getString("mq_host", MQTT_HOST_DEFAULT);
  g_port = (uint16_t)p.getInt("mq_port", MQTT_DEFAULT_PORT);
  g_user = p.getString("mq_user", "");
  g_pass = p.getString("mq_pass", "");
  g_base = p.getString("mq_base", MQTT_BASE_TOPIC_DEFAULT);
  bool en = p.getBool("mq_en", false);
  p.end();

  g_enabled = en && g_host.length() && WiFi.isConnected();
  if (!g_enabled) {
    Serial.println("[mqtt] disabled (off, no host, or not on a Wi-Fi network)");
    return;
  }

  uint64_t mac = ESP.getEfuseMac();
  char idbuf[32];
  snprintf(idbuf, sizeof(idbuf), "ipixel-%04X%08X",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  g_clientId = idbuf;

  net.setInsecure();                 // TLS encryption, no server-cert check
  client.setServer(g_host.c_str(), g_port);
  client.setBufferSize(1536);        // room for text payloads (default is 256)
  client.setKeepAlive(30);
  client.setCallback(onMessage);
  Serial.printf("[mqtt] enabled host=%s:%u base=%s\n",
                g_host.c_str(), g_port, g_base.c_str());
}

void loop() {
  if (!g_enabled) return;
  if (client.connected()) {
    client.loop();
  } else if (millis() > g_nextTry) {
    g_nextTry = millis() + 5000;     // retry at most every 5 s
    connect();
  }
}

bool   enabled()    { return g_enabled; }
bool   connected()  { return g_enabled && client.connected(); }
String statusText() {
  if (!g_enabled) return "off";
  return client.connected() ? "connected" : "connecting";
}

}  // namespace Mqtt
