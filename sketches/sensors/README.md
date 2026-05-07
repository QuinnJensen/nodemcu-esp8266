# sensors

Formerly `the_mountain`. NodeMCU ESP8266 sketch that reads a DS18B20 1-Wire
temperature network and a KIB K101 water-level probe, publishes MQTT heartbeats,
and serves a Prometheus `/metrics` endpoint.

## Build

```sh
# From monorepo root
pio run -e sensors
pio run -e sensors -t upload
pio run -e sensors -t uploadfs
```

## Pin assignments

| Pin | GPIO | Function |
|-----|------|----------|
| D0 | 16 | PROBE_ON (water probe enable) |
| D2 | 4 | 1-Wire bus (DS18B20) |
| D3 | 0 | Force-portal button (FLASH) |
| D4 | 2 | Blue LED (active LOW) |
| D5 | 14 | I2C SDA (SSD1306 OLED) |
| D6 | 12 | I2C SCL (SSD1306 OLED) |
| A0 | — | Water probe analog sense |

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
