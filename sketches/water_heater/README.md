# water_heater

NodeMCU ESP8266 sketch for a hot-water-heater SSR power controller.
Based on `water_heater_v2.ino`. **Not yet refactored** to use the
`esp8266-common` shared library — kept verbatim for a future refactor pass.

## Build

```sh
pio run -e water_heater
pio run -e water_heater -t upload
pio run -e water_heater -t uploadfs
```

## Pin assignments

| Pin | GPIO | Function |
|-----|------|----------|
| D3 | 0 | Force-portal button |
| D5 | 14 | I2C SDA (SSD1306 OLED) |
| D6 | 12 | I2C SCL (SSD1306 OLED) |
| D7 | 13 | SSR simulation output |

## MQTT topics

```
<baseTopic>/command
<baseTopic>/status
<baseTopic>/results
```

## MQTT commands

```json
{"type":"command", "powerpercent": 37}
{"type":"ls"}
{"type":"calibrate", "actualpowerwatts": 612.0}
{"type":"purgecalibration"}
```

## Persistent files (LittleFS)

- `config.json` — MQTT host/port, base topic
- `cal.json` — power calibration table (10 entries, 10–100 %)

## Shared patterns (future lib candidates)

- WiFiManager captive portal
- MQTT connect/publish/callback
- SSD1306 OLED spinner + status line
- LittleFS config load/save
- `ipToString`, `timeToString`, `currentTimestampString`
- Timer1 ISR Bresenham SSR modulator (unique to this sketch)
