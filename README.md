# nodemcu-esp8266

PlatformIO monorepo for a set of NodeMCU v2 (ESP8266) home-automation sketches.
All three sketches share a common set of library dependencies and are built from a
single root `platformio.ini`.

---

## Sketches

| Sketch | PlatformIO env | Description |
|---|---|---|
| [`sketches/sensors`](sketches/sensors) | `sensors` | Multi-sensor node: DS18B20 temperature bus, capacitive water-level probe, OLED display, MQTT telemetry, Prometheus metrics endpoint, and WiFiManager captive-portal setup. |
| [`sketches/water_heater`](sketches/water_heater) | `water_heater` | Water-heater controller. |
| [`sketches/uhf_modulator`](sketches/uhf_modulator) | `uhf_modulator` | UHF modulator controller. |

---

## Hardware (sensors sketch)

| Component | Notes |
|---|---|
| NodeMCU v2 (ESP8266) | Main MCU |
| SSD1306 128×64 OLED | I²C display — status, spinner, sensor readings |
| DS18B20 | 1-Wire temperature sensors (up to 8) |
| Capacitive water-level probe | ADC on A0; powered via a GPIO (probeOnPin) |
| Blue LED | Activity indicator; gated by `ledEnabled` config flag |

---

## Project layout

```
nodemcu-esp8266/
├── platformio.ini          # Monorepo root — defines all three envs
├── lib/                    # Shared libraries (available to sensors env)
└── sketches/
    ├── sensors/            # Temperature + water-level node
    ├── water_heater/       # Water-heater controller
    └── uhf_modulator/      # UHF modulator controller
```

---

## Building & flashing

Requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).

```bash
# Build a specific sketch
pio run -e sensors

# Build all sketches
pio run

# Flash firmware
pio run -e sensors -t upload

# Upload SPIFFS/LittleFS filesystem image (sensors sketch web UI)
pio run -e sensors -t uploadfs

# Open serial monitor
pio device monitor -e sensors
```

---

## sensors sketch — feature overview

### Connectivity
- **WiFiManager** captive portal for first-time Wi-Fi setup; hold FLASH at boot to re-enter.
- **MQTT** publishes temperature and water-level telemetry on a configurable interval.
- **Prometheus** metrics endpoint (`http://<ip>:<port>/metrics`) for scraping.

### Sensors
- Up to 8 × DS18B20 on a shared 1-Wire bus; addresses resolved to friendly names stored in LittleFS.
- Capacitive water-level probe with configurable ADC thresholds (`no_probe`, `>40 gal`, `15–40 gal`, `5–15 gal`, `<5 gal`).

### Display & indicators
- 128×64 OLED shows device ID, Wi-Fi/MQTT status, RSSI, sensor readings, and water level.
- Animated 8-dot spinner with a centre-dot pulse that fires on DS18B20 bus activity.
- Blue LED mirrors the spinner centre-dot for DS18B20 scans and temperature conversions; stays on for the full duration of a water-probe measurement. Both are gated by the `ledEnabled` configuration flag.

### Web UI
Served from LittleFS (`data/index.html`). Provides live sensor readings, configuration editor (MQTT, Prometheus, thresholds, LED toggle), and MQTT command controls.

---

## Configuration

All runtime settings are stored in LittleFS as JSON and editable via the web UI or MQTT commands. Key fields:

| Field | Description |
|---|---|
| `mqttserver` / `mqttport` | MQTT broker address |
| `deviceid` | MQTT topic prefix and display name |
| `prometheusport` | Port for the `/metrics` endpoint |
| `ledEnabled` | Enable/disable blue LED activity flashes |
| `waterThresholds` | Array of 5 ADC thresholds for water-level classification |
| `waterHeartbeatIntervalMs` | How often to publish water-level readings |

---

## License

MIT
