# Driving the sign from Home Assistant (remote, over MQTT)

Because the sign sits on an isolated guest network, nothing can reach its web
page by IP. Instead it dials **out** to an MQTT broker, and Home Assistant
publishes commands to that broker. Control works from anywhere, nothing is
exposed inbound.

```
Your phone ──► Home Assistant ──► HiveMQ Cloud (broker) ──► the sign
  (location/           (publishes          (relays)         (subscribed to
   buttons)            ipixel/cmd)                           ipixel/cmd)
```

## Prerequisites
- The sign flashed with the current firmware (MQTT + content library) and
  already joined to the guest Wi-Fi (configure that on its own hotspot at
  `192.168.4.1`).
- A running **Home Assistant** instance. If you don't have one yet, install
  Home Assistant OS on a Raspberry Pi / mini PC / VM (see
  https://www.home-assistant.io/installation/). The HA box can be anywhere
  with internet; it does **not** need to be on the sign's network.

---

## Part A - HiveMQ Cloud (the broker in the middle)
1. Sign up at https://www.hivemq.com/mqtt-cloud-broker/ and create a **free
   Serverless** cluster.
2. On the cluster overview, note the **Cluster URL** (host, like
   `abcd1234.s1.eu.hivemq.cloud`) and that the TLS port is **8883**.
3. Open **Access Management → Credentials** and create a **username + password**.
   Keep these. You'll use the same pair twice: once on the sign, once in HA.

## Part B - Point the sign at the broker
On the sign's hotspot page (join its Wi-Fi, go to `192.168.4.1`) open the
**Remote (MQTT)** card:
- Host: your HiveMQ host, Port: `8883`, Base topic: `ipixel`
- Username / Password: the HiveMQ credentials
- Tick **Enable**, **Save & reboot**.

After it reboots (and is on the guest Wi-Fi with internet), the card should
show **MQTT: connected**. It now listens on `ipixel/cmd` and publishes
`ipixel/status` (online/offline).

## Part C - Connect Home Assistant to the same broker
1. In HA: **Settings → Devices & Services → + Add Integration → MQTT**.
2. Choose "Manually enter connection details":
   - Broker: your HiveMQ host (e.g. `abcd1234.s1.eu.hivemq.cloud`)
   - Port: **8883**
   - Username / Password: the HiveMQ credentials
   - Expand advanced options and set **Broker certificate validation: Auto**
     (HiveMQ uses a public CA, so TLS verifies cleanly).
3. Submit. The MQTT integration should connect.
4. Quick test: **Settings → Devices & Services → MQTT → Configure →
   Publish a packet**, topic `ipixel/cmd`, payload `hi there`. The sign should
   scroll "hi there". If it does, the whole pipe works.

## Part D - Install the helpers, scripts and dashboard
1. Copy **`ipixel_package.yaml`** (this folder) into your HA config at
   `<config>/packages/ipixel_package.yaml`. Enable packages in
   `configuration.yaml` if you haven't:
   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```
2. In **`ipixel_package.yaml`**, edit the two example automations: replace
   `person.YOU` with your HA person and `zone.work` with your zone.
3. **Developer Tools → YAML → Check configuration**, then **Restart**.
4. Add the dashboard card: open a dashboard → **Edit → + Add Card → Manual** →
   paste the contents of **`dashboard.yaml`**.

## Part E - Use it
On the dashboard: type a message + hex colour, tap **Send text**; use the
brightness slider + **Set bright**; **On/Off**, **Clear**; and **Rotate on/off**
to start/stop the sign cycling your saved content library.

## Part F - Geofencing (location-triggered messages)
1. Install the **Home Assistant Companion app** on your phone and sign in to
   your HA. Allow **Location** (Always) so it reports your position.
2. Define zones in **Settings → Areas & Zones** (e.g. a "Work" zone).
3. The two automations in `ipixel_package.yaml` fire on entering/leaving the
   zone and publish a message. Edit the text/colours to taste, or duplicate
   for more zones.

**Remote-access note:** for geofencing to work while you're away, your phone's
Companion app must reach your HA from the internet. The simple path is **Home
Assistant Cloud (Nabu Casa)**, a paid subscription that also powers remote
dashboards; alternatively expose HA behind a secured external URL. HA's MQTT
publishing is outbound and needs nothing inbound - it's only the phone →
HA location updates that need HA reachable.

*Lighter alternative with no HA remote access:* an **Apple Shortcuts**
(iPhone) or **Tasker** (Android) automation that publishes directly to
HiveMQ on a location trigger, skipping HA entirely. Same `ipixel/cmd` topic
and payloads.

---

## Command reference (topic `ipixel/cmd`)
A non-JSON payload is treated as scrolling text. JSON forms:

| Action | Payload |
| --- | --- |
| Text | `{"type":"text","text":"Hello","r":255,"g":128,"b":0,"anim":1,"speed":80}` |
| Power | `{"type":"power","on":true}` |
| Brightness | `{"type":"brightness","level":60}` |
| Clear | `{"type":"clear"}` |
| Auto-rotate | `{"type":"rotate","on":true}` |
| Play saved item | `{"type":"play","id":"a1b2c3"}` (id from the sign's `/api/playlist`) |

Animations: 0 static, 1 scroll-left, 2 scroll-right, 3 up, 4 down, 5 blink,
6 fade, 7 snowflake.

## Troubleshooting
- **Sign card never shows MQTT: connected** - double-check host/port/user/pass;
  confirm the sign is on Wi-Fi with internet (not just its own hotspot). If the
  guest firewall blocks 8883, you may need MQTT-over-WebSocket on 443 (ask and
  we'll add it).
- **HA MQTT won't connect** - wrong port (must be 8883) or TLS validation; set
  it to Auto.
- **Publish works but sign doesn't react** - the sign must be BLE-connected to
  the LED panel (and the official iPixel app closed). Check `ipixel/status`.
- **Message shows `?` characters** - the font is ASCII only; avoid accents/emoji.
