# esp8266-common

Shared PlatformIO library for NodeMCU / ESP8266 sketches in this monorepo.

## Modules

| Module | Description |
|---|---|
| `app_config` | LittleFS-persisted MQTT/device configuration. Manages runtime feature switches (`sensor_network_enabled`, `water_probe_enabled`). Sketches can add/load extra fields via `setAppConfigExtraHooks()`. |
| `app_state` | Runtime state variables. 1-Wire / water-probe sections are compile-time gated by `SHARED_LIB_USE_ONEWIRE` / `SHARED_LIB_USE_WATER_PROBE`. |
| `wifi_portal` | WiFiManager captive portal + reconnect logic. Display callbacks are sketch-supplied via `setWifiPortalDisplayCallbacks()`. |
| `mqtt_client` | PubSubClient wrapper with a non-blocking reconnect state machine. Publish + subscribe helpers. |
| `metrics_server` | Prometheus `/metrics` HTTP endpoint. Sketches register `setMetricsExtra()` to append device-specific metrics and `setMetricsNamePrefix()` to namespace them. Suppresses metrics for disabled features. |
| `display_ui` | SSD1306 OLED status rendering and animated spinner. Sketches register `setDisplayBodyRenderer()` to draw their middle-of-screen content. Handles disabled feature display text. |
| `web_ui` | Web UI plumbing (status/config/console/fs endpoints, `/api/config/features`). Sketches add device-specific JSON fields, routes, console help, and deferred actions via the `setWeb*Fn()` hooks. |
| `console_log` | Ring-buffer text console (32 entries × 256 chars) consumed by the browser via `/api/console/log?after=<seq>`. |
| `sensor_bus` | DS18B20 1-Wire bus scan + temperature reading (compile in with `SHARED_LIB_USE_ONEWIRE`). Strictly halts all 1-Wire bus activity when `sensor_network_enabled` is false. |
| `sensor_names` | Persistent address→name mapping in `/sensor_names.json`. Compile in with `SHARED_LIB_USE_ONEWIRE`. |
| `util` | IP-to-string, timestamp, topic sanitizer helpers, NTP setup. |

## Sketches that use this library

- **sensors** (`sketches/sensors/`) — DS18B20 temperatures + water probe + Prometheus
- **water_heater** (`sketches/water_heater/`) — SSR power controller + optional DS18B20
- **uhf_modulator** (`sketches/uhf_modulator/`) — 433 MHz OOK transmitter + optional DS18B20

## Compile-time flags

| Flag | Effect |
|---|---|
| `SHARED_LIB_USE_ONEWIRE` | Compile in DS18B20 1-Wire driver, sensor_names, and related state |
| `SHARED_LIB_USE_WATER_PROBE` | Compile in `waterTopic`, `waterAdcRaw` and friends |
| `SHARED_LIB_DEFAULT_DEVICE_ID` | Default device ID string |
| `SHARED_LIB_DEFAULT_BASE_TOPIC` | Default MQTT base topic |
