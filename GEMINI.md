# Project: Firmware Mission Control
**Hardware Platform:** NodeMCU ESP8266 V3
**Framework:** Arduino / PlatformIO

## 1. Environment Configuration
* **Platform:** `esp8266`
* **Board:** `nodemcuv2`
* **Framework:** `arduino`
* **Toolchain Specs:** Standard GCC for Xtensa, managed via PlatformIO.
* **Build Stamping:** Automatically includes `BUILD_VERSION` (date/time + git hash) injected via `scripts/set_build_version.py`.

## 2. Library Dependencies
* **OneWire/DallasTemperature:** DS18B20 handling (often used via shared `sensor_bus`).
* **WiFiManager:** Captive portal for credential and MQTT config.
* **Adafruit SSD1306:** For 128x64 OLED display.
* **ArduinoJson:** Serialization for state reporting and commands.

## 3. Architecture & Design Patterns

### 3.1. Modular Shared Library (`lib/shared`)
* **Shared Core:** All mission sketches MUST use `lib/shared` for WiFi, MQTT, AppConfig, and Display UI to maintain consistent behavior across the fleet.
* **Display UI:** State-driven rendering. Sketches provide a `DisplayBodyRenderer` callback to inject mission-specific data into the central OLED layout (Header/Body/Status format).
* **WiFi Portal:** Standardized SSID format `espXXXX-SSSS` where `SSSS` is the sketch suffix (`sens`, `heat`, `uhf`).

### 3.2. Runtime Architecture
* **Strict Non-Blocking:** All network (MQTT, WiFi) and hardware (Water Probe, SSR Modulator) logic must be asynchronous state machines. Never use `delay()` in the `loop()`.
* **Resource Serialization:** ESP8266 web fetches must be serialized (single-flight) to prevent sluggishness under concurrent HTTP requests.
* **Async SSR Control:** High-frequency switching (e.g., Water Heater SSR) uses Timer1 interrupts or Bresenham-style algorithms to avoid jitter from loop latency.

### 3.3. UI Design Principles
* **Single-Request Web UI:** All CSS and JS are inlined into `index.html` to minimize HTTP overhead on the ESP8266.
* **Adaptive Polling:** The Web UI implements adaptive backoff; it slows down polling on failure and recovers on success to protect MCU stability.
* **OLED Visual Language:** Consistent use of the top-right spinner to indicate MQTT/Network activity and a bottom status line for transient system messages.

## 4. Hardware Pin Assignments
* **A0:** Water Probe (Analog input).
* **D0:** Service Barrel Level Probe.
* **Control Output:** SSR Relay (SPST-NO 25A) for system switching.
* **OneWire Bus:** Typically GPIO14 (D5), but check `pins_and_constants.h` per sketch.

## 5. Safety & Integrity
* **Reconfig Gateway:** Hold the FLASH button (GPIO0) during boot to force the WiFi/MQTT config portal.

