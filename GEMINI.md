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
* **Resource Serialization:** ESP8266 web fetches must be serialized (single-flight) to prevent sluggishness.
* **Async MQTT Handshake:** MQTT connections MUST use a non-blocking TCP handshake (short initial timeout + polling) to prevent loop freezes during network outages.
* **JSON Streaming:** Large API responses MUST use direct streaming (`serializeJson(doc, client)`) instead of buffering in a `String` to prevent heap exhaustion.
* **Stack Hardening:** Large JSON documents (>= 1KB) MUST be heap-allocated (`DynamicJsonDocument`) to prevent stack overflows on the fragile 4KB ESP8266 stack.
* **60Hz Full-Cycle SSR Control:** High-frequency switching uses a 60Hz Bresenham algorithm to drive full AC cycles. Gate pulses are extended (~18.5ms) using a main-loop one-shot to reliably span zero-crossings without hardware detection.
* **IRAM-Safe ISRs:** All ISR code MUST be 100% self-contained in IRAM. Avoid multiplication (`*`) as it calls non-IRAM software helpers; use bitwise shifts (`<<`, `>>`) instead.
* **DRAM ISR Data:** All variables accessed within an ISR MUST be explicitly placed in DRAM using `__attribute__((section(".iram.data")))` to prevent crashes during Flash memory lockouts (e.g., during LittleFS writes or sensor scans).

### 3.4. Update & Management
* **Dual-Strategy OTA:** All mission sketches support both `ArduinoOTA` (Network Push) for development and `ESP8266HTTPUpdateServer` (Web Upload) for production.
* **Safety Gating:** High-power hardware (SSR) MUST be forced to zero via the `setOtaStartCallback()` before any OTA update proceeds.
* **Persistent Naming:** 1-Wire sensors are identified by 64-bit ROM addresses and assigned human-readable names persisted in `sensors.json` via the shared `sensor_names` module.

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

