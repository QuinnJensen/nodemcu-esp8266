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

- `config.json` — MQTT host/port, base topic, device ID, Prometheus port, water config
- `sensornames.json` — DS18B20 address → friendly name map
