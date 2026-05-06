# esp8266-common

Shared PlatformIO library for NodeMCU / ESP8266 sketches in this monorepo.

## Modules

| Module | Description |
|---|---|
| `app_config` | LittleFS-persisted MQTT/device configuration |
| `app_state` | Runtime state variables (shared across modules) |
| `wifi_portal` | WiFiManager captive portal + reconnect logic |
| `mqtt_client` | PubSubClient wrapper, connect/subscribe/publish helpers |
| `mqtt_commands` | MQTT command dispatcher |
| `metrics_server` | Prometheus `/metrics` HTTP endpoint |
| `display_ui` | SSD1306 OLED status rendering and spinner |
| `sensor_bus` | DS18B20 1-Wire bus scan and temperature reading |
| `sensor_names` | Persistent address→name mapping (LittleFS) |
| `water_probe` | KIB K101 resistive water-level probe |
| `web_ui` | Embedded HTTP web interface |
| `scheduler` | Lightweight interval task scheduler |
| `util` | IP-to-string, timestamp, topic sanitizer helpers |

## Sketches that use this library

- **sensors** (`sketches/sensors/`) — DS18B20 temperature network + water probe + Prometheus
- **water_heater** (`sketches/water_heater/`) — SSR power controller (not yet refactored to use lib)
- **uhf_modulator** (`sketches/uhf_modulator/`) — 433 MHz OOK transmitter (not yet refactored to use lib)

## Future refactoring candidates

Both `water_heater` and `uhf_modulator` share the following patterns with the sensors sketch (and therefore with this library) and are good candidates for future extraction:

- `wifi_portal` — identical WiFiManager setup pattern
- `mqtt_client` — `publishJsonDocToTopic`, `mqttConnect`, `mqttCallback`, `ensureWifi`
- `metrics_server` — `ESP8266WebServer` Prometheus endpoint
- `display_ui` — SSD1306 spinner and status line
- `util` — `sanitizeTopicPart`, `ipToString`, `timeToString`, `currentTimestampString`
- `app_config` — LittleFS `config.json` load/save pattern
