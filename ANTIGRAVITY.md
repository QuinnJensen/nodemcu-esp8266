# Project: Firmware Mission Control
**Hardware Platform:** NodeMCU ESP8266 V3
**Framework:** Arduino / PlatformIO

## 1. Environment Configuration
* **Project Path:** `/home/joe-mcu/m/nodemcu-esp8266`
* **Platform:** `esp8266`
* **Board:** `nodemcuv2`
* **Framework:** `arduino`
* **Toolchain Specs:** Standard GCC for Xtensa, managed via PlatformIO Core 6.2+.
* **CLI & Environment:** PlatformIO Core is installed in `~/.platformio/penv` and symlinked to `~/.local/bin/pio` (included in `$PATH`), allowing direct `pio` invocations.
* **Build Commands:**
  * Build sensors: `pio run -e sensors`
  * Build water heater: `pio run -e water_heater`
  * Build UHF modulator: `pio run -e uhf_modulator`
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
* **WiFi Portal:** Standardized SSID format `espXXXX-SSSS` where `SSSS` is the sketch suffix (`sens`, `heat`, `uhf`). The portal MUST allow configuration of both MCU and Sensor MQTT base topics.

### 3.2. Runtime Architecture & Stability
* **Strict Non-Blocking:** All network (MQTT, WiFi) and hardware (Water Probe, SSR Modulator) logic must be asynchronous state machines. Never use `delay()` in the `loop()`.
* **Resource Serialization:** ESP8266 web fetches must be serialized (single-flight). JSON responses MUST use direct streaming (`serializeJson(doc, client)`) to prevent heap exhaustion.
* **Async MQTT Handshake:** MQTT connections MUST use a non-blocking TCP handshake (short initial timeout + background polling) to prevent loop freezes during network outages.
* **Stack Hardening:** Large JSON documents (>= 1KB) MUST be heap-allocated (`DynamicJsonDocument`) to prevent stack overflows on the fragile 4KB ESP8266 stack.
* **120Hz Jitter-Free SSR Control:** High-frequency switching uses a 120Hz Timer1 ISR. Bresenham decisions happen every 2nd tick (60Hz). Gate pulses are deterministic (16.67ms) and handled entirely within the ISR to eliminate main-loop jitter.
* **IRAM-Safe ISRs:** All ISR code MUST be 100% self-contained in IRAM. Avoid multiplication (`*`) and division (`/`); use bitwise shifts (`<<`, `>>`) instead.
* **DRAM ISR Data:** All variables accessed within an ISR MUST be explicitly placed in DRAM using `__attribute__((section(".iram.data")))` to prevent crashes during Flash memory lockouts.

### 3.3. Feature Switches
* **Granular Controls:** Supports independent runtime toggles for `water_probe_enabled` and `sensor_network_enabled`, configured via a dedicated panel on the Web UI Settings tab (both enabled by default).
* **Strict Zero-Probing:** When a feature is disabled, all physical bus and ADC probing MUST immediately halt (return early from `scanSensors()`, `requestTemperatureConversion()`, `readTemperatures()`, `beginWaterSample()`). Zero 1-Wire transactions and zero ADC conversions should occur.
* **Payload Field Omission:** Aggregated MQTT status heartbeats (`publishAggregateStatus`, `publishHeaterStatus`, `publishUhfStatus`) and web `/api/status` payloads MUST completely omit disabled telemetry fields (`sensorcount`, `simulated`, `networkdetected`, `sensors`, and `water` objects), rather than reporting zeros, nulls, or empty arrays.
* **API Endpoint Guarding:** Dedicated status endpoints (`/api/temps`, `/api/water`) must return `{"enabled": false}` when disabled. Manual trigger endpoints (`/api/sensors/scan`, `/api/water/sample`) must reject requests with HTTP 400.
* **Web UI Graceful Degradation:** Dashboard tabs and cards corresponding to disabled features are greyed out/blanked and contain hotlinks back to the Settings tab.
* **OLED Visibility:** Disabled features are hidden or excluded from display rotations on the on-board OLED display.
* **MQTT & Metrics Suppression:** 
  * MQTT commands targeted at a disabled feature return a JSON error reply.
  * Per-sensor and water MQTT topics are suppressed when the feature is disabled.
  * Prometheus metrics corresponding to the disabled feature are suppressed/lobotomized.

### 3.4. Water Probe v3 Architecture
* **Constant Voltage Circuit:** The probe uses a constant voltage source on `A0` (eliminating the old `PROBE_EN` GPIO `D0` line, freeing `D0` as unused).
* **Low-Noise Sampling Routine:** Every 15 seconds, the A0 input is sampled using a simple average of 3 readings spaced by 1ms (with the first read discarded to clear any multiplexer cache state).
* **Wi-Fi Modem-Sleep Suppression:** Calling `WiFi.setSleepMode(WIFI_NONE_SLEEP)` during boot keeps the RF block permanently on. This stabilizes the internal bandgap reference and the 3.3V power rails, achieving high ADC accuracy and eliminating current draw ripples.
* **MQTT Topic Alignment:** Water probe status is published under `config.mcuBaseTopic` (e.g. `stat/mcu/mountain/mwater/water`) instead of the general `config.sensorBaseTopic` (`stat/w1/...`), aligning telemetry with the MCU administrative hierarchy.
* **Real-time Voltage Indicators:** The thresholds card dynamically calculates and displays corresponding DC voltages ($\text{Voltage} = \text{ADC} \times \frac{3.3}{1023}$) next to threshold inputs, updating in real time via JavaScript `oninput` handlers.

### 3.5. Hierarchical MQTT Scheme
* **Split Roots:** All devices MUST support separate base topics for device management and telemetry.
  * **MCU Base Topic:** (default `stat/mcu` or `stat/heater`) used for heartbeats, commands, and results.
  * **Sensor Base Topic:** (default `stat/w1`) used for granular per-sensor telemetry.
* **Consistent Heartbeats:** The primary status heartbeat MUST include technical readouts (Power %, Watts, Amps) using standardized keys: `power_pct`, `power_w`, and `current_a`.

### 3.6. UI Design Principles
* **Single-Request Web UI:** All CSS and JS are inlined into `index.html`.
* **Mandatory Web Tabs:** All mission UIs MUST include:
  * **Dashboard:** Featuring a "Pinout Reference" card documenting D1-D7 and A0 roles.
  * **Settings:** Supporting non-clobbering live updates for sensor tables and separate MQTT topic roots.
  * **System:** Providing an OTA update portal link and detailed device info (Chip ID, Uptime, Build Version).
* **OLED Visual Language:** Consistent use of the top-right spinner for activity and a bottom line for IP/Status.
  * **Power Line:** Displays `%`, `W`, and `A` (0.1A granularity).
  * **Sensor Line:** MUST rotate through all detected 1-Wire sensors every 3 seconds (when enabled).

### 3.7. Update & Build Management
* **Build Stamping:** `set_build_version.py` MUST explicitly `touch` `lib/shared/app_state.cpp` to force recompilation and ensure every binary contains a fresh timestamp and git hash.
* **Dual-Strategy OTA:** All sketches support `ArduinoOTA` (Push) and `ESP8266HTTPUpdateServer` (Web Upload).
* **Filesystem Synchronization (Mandatory Check):** Whenever firmware is updated or deployed, the developer/agent MUST check whether web UI assets or LittleFS files have changed and flash the LittleFS filesystem (`littlefs.bin` / `uploadfs`) in lockstep with the firmware to prevent UI/capability mismatches.
* **HTTP Update Deployment:** When ArduinoOTA (port 8266) is blocked across routed subnets or firewalls, firmware and filesystem binaries can be deployed reliably via HTTP POST to `/update` (`curl -F "firmware=@firmware.bin" ...` or `curl -F "filesystem=@littlefs.bin" ...`).
* **Sensor Diagnostics Reference:** For suspected sensor hardware failures or erratic readings passing CRC, consult [`DS18B20-FAILURE-ANALYSIS.md`](file:///home/joe-mcu/m/nodemcu-esp8266/DS18B20-FAILURE-ANALYSIS.md) for clone silicon ROM fingerprints and moisture ingress failure modes.
* **Safety Gating:** High-power hardware (SSR) MUST be forced to zero via the `setOtaStartCallback()` before any update proceeds.
* **Persistent Naming:** 1-Wire sensors are identified by 64-bit ROM addresses and assigned human-readable names persisted in `sensors.json`.

## 4. Hardware Pin Assignments
* **A0:** Water Probe (Analog input).
* **D0:** Unused (previously Probe ON - excitation removed).
* **D1:** SSR Relay Control (Safe I/O pin, no SPI conflict).
* **D2:** OneWire Bus (DS18B20).
* **D3:** Force-Portal/Reconfig Button (GPIO0).
* **D4:** Onboard Status LED (Blue).
* **D5/D6:** I2C Bus (SSD1306 OLED).

## 5. Safety & Integrity
* **Reconfig Gateway:** Hold the FLASH button (GPIO0) during boot to force the WiFi/MQTT config portal.
