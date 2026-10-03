# sensors

Formerly `the_mountain`. NodeMCU ESP8266 sketch that reads a DS18B20 1-Wire
temperature network and a KIB K101 water-level probe, publishes MQTT heartbeats,
and serves a Prometheus `/metrics` endpoint.

## Build & Deploy

```sh
# From monorepo root
pio run -e sensors

# Local USB or local subnet OTA:
pio run -e sensors -t upload
pio run -e sensors -t uploadfs

# Routed subnet HTTP OTA (reliable when port 8266 is unreachable):
curl -F "firmware=@.pio/build/sensors/firmware.bin" http://<ip>/update
curl -F "filesystem=@.pio/build/sensors/littlefs.bin" http://<ip>/update
```

> **Note:** Always update the LittleFS filesystem (`uploadfs` or `/update`) whenever firmware is updated, especially if web assets or feature switches have changed.

## Pin assignments

| Pin | GPIO | Function |
|-----|------|----------|
| D0 | 16 | Unused (excitation removed in water probe v3) |
| D2 | 4  | 1-Wire bus (DS18B20) |
| D3 | 0  | Force-portal button (FLASH) |
| D4 | 2  | Blue LED (active LOW) |
| D5 | 14 | I2C SDA (SSD1306 OLED) |
| D6 | 12 | I2C SCL (SSD1306 OLED) |
| A0 | —  | Water probe analog sense (constant voltage circuit) |

## Feature Switches

Two runtime feature switches are configured on the Settings page or via `POST /api/config/features`:
- `sensor_network_enabled` (default `true`): When disabled, all physical 1-Wire bus scanning and temperature conversion requests cease immediately. Per-sensor MQTT topics are suppressed, `/api/temps` returns `{"enabled": false}`, manual scans are rejected with HTTP 400, and sensor fields (`sensorcount`, `simulated`, `networkdetected`, `sensors`) are completely omitted from MQTT aggregate status heartbeats and `/api/status`.
- `water_probe_enabled` (default `true`): When disabled, ADC sampling stops. Water MQTT publishes are suppressed, `/api/water` returns `{"enabled": false}`, manual samples are rejected with HTTP 400, and the `water` object is completely omitted from heartbeats, `/api/status`, and `/api/config`. On the SSD1306 OLED display, the Water status line is replaced with a 3rd sensor row (displaying 3 sensors instead of 2).

## 1-Wire Diagnostics & Sensor Failure Analysis

If a DS18B20 sensor begins reporting erratic or jumping temperatures while still passing CRC checks, consult the detailed field guide in [`DS18B20-FAILURE-ANALYSIS.md`](../../DS18B20-FAILURE-ANALYSIS.md) covering clone silicon identification (ROM serial with `00 00 00`), capacitive register degradation, and moisture ingress in waterproof stainless capsules.

## MQTT topics

```
<baseTopic>/<deviceId>/command
<baseTopic>/<deviceId>/status
<baseTopic>/<deviceId>/results
<baseTopic>/<deviceId>/water
<baseTopic>/<deviceId>/sensor/<sensorName>
```

## Persistent files (LittleFS)

- `config.json` — MQTT host/port, base topic, device ID, Prometheus port, timezone, water config
- `sensornames.json` — DS18B20 address → friendly name map

## Web console

The Settings tab exposes a **Timezone** card holding a POSIX TZ string used
for the OLED clock, MQTT timestamps, and Console log lines. Default:
`MDT7MST,M3.2.0,M11.1.0`. Changes apply immediately and are persisted to
`config.json`.

The Console tab accepts:

- Slash commands handled in the browser/firmware:
  - `/help` — show available commands
  - `/status` — log current heap / uptime / link state
  - `/clear` — clear the console pane
- Bare-word shortcuts that expand to the matching MQTT command JSON and run
  through the same handler the broker uses: `scan`, `status`, `heartbeat`,
  `water`, `waterstatus`.
- Raw JSON, e.g. `{"command":"scan"}`, dispatched verbatim.

Every MQTT publish is mirrored to the console as `PUB <topic> <full JSON
payload>` (and `PUB-FAIL …` on failure). Ring buffer holds the last 32
entries, 256 chars each.
