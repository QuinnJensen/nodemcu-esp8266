# nodemcu-esp8266

PlatformIO monorepo for a set of NodeMCU v2 (ESP8266) home-automation sketches.
All three sketches share a common `lib/shared/` library and are built from a
single root `platformio.ini`.

---

## Sketches

| Sketch | PlatformIO env | Description |
|---|---|---|
| [`sketches/sensors`](sketches/sensors) | `sensors` | Multi-sensor node: DS18B20 temperature bus, capacitive water-level probe, OLED display, MQTT telemetry, Prometheus metrics endpoint, web UI, and console. |
| [`sketches/water_heater`](sketches/water_heater) | `water_heater` | Hot-water-heater SSR power controller with jitter-free 120 Hz Bresenham modulator, calibration table, thermal fail-safe, web UI and console. |
| [`sketches/uhf_modulator`](sketches/uhf_modulator) | `uhf_modulator` | 433 MHz OOK transmitter with stored timing-profile and code tables, optional DS18B20 temperature monitoring, web UI and console. |

All three sketches share the `lib/shared/` library for WiFi captive
portal, MQTT client (non-blocking background reconnect), Prometheus metrics endpoint,
SSD1306 display (with IP readout), text console ring buffer, web UI plumbing (with System/OTA tab), LittleFS
config persistence, and (optional) 1-Wire DS18B20 driver.

---

## Project layout

```
nodemcu-esp8266/
├── platformio.ini           # Monorepo root — defines all three envs
├── lib/shared/              # Shared library (linked by every sketch)
└── sketches/
    ├── sensors/src          # Temperature + water-level node
    ├── water_heater/src/    # Water-heater controller
    └── uhf_modulator/src/   # UHF modulator controller
```

Each sketch keeps a `data/` directory with its `index.html`, uploaded to
LittleFS via `pio run -e <env> -t uploadfs`.

---

## Shared library modules (`lib/shared/`)

| Module | Description |
|---|---|
| `app_config` | LittleFS-persisted MQTT/device configuration with sketch-specific extension hooks |
| `app_state` | Runtime state variables (1-Wire / water-probe sections compile-time gated) |
| `wifi_portal` | WiFiManager captive portal + reconnect logic |
| `mqtt_client` | PubSubClient wrapper, non-blocking background TCP handshake, publish/subscribe helpers |
| `metrics_server` | Prometheus `/metrics` HTTP endpoint with sketch-specific `extra` hook |
| `display_ui` | SSD1306 OLED status rendering, spinner, sketch-specific body renderer hook |
| `web_ui` | Web UI plumbing (streaming JSON, file browser, System/OTA tab); sketches add device-specific routes via hooks |
| `ota_update` | Dual-strategy OTA: ArduinoOTA (network push) and Web Update Server (/update) |
| `console_log` | Ring-buffer text console for browser-side `/api/console/log` polling |
| `sensor_bus` | DS18B20 1-Wire bus scan + temperature reading (compile-time gated) |
| `sensor_names` | Persistent address→name mapping (compile-time gated) |
| `util` | IP-to-string, timestamp, topic sanitizer helpers |

### Compile-time flags

Sketches enable optional shared-lib features via build flags:

| Flag | Effect |
|---|---|
| `SHARED_LIB_USE_ONEWIRE` | Compile in DS18B20 1-Wire driver |
| `SHARED_LIB_USE_WATER_PROBE` | Compile in capacitive water-probe state machine support (sensors only) |
| `SHARED_LIB_DEFAULT_DEVICE_ID` | Default device ID string baked in |
| `SHARED_LIB_DEFAULT_BASE_TOPIC` | Default MQTT base topic baked in |

---

## Building & flashing

Requires [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).

```bash
pio run -e sensors
pio run -e water_heater
pio run -e uhf_modulator

pio run -e <env> -t upload
pio run -e <env> -t uploadfs   # uploads data/ to LittleFS

pio device monitor -e <env>
```

---

## License

MIT
