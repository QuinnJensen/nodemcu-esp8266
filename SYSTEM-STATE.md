# Project System State - Firmware Mission Control

This file maintains the active grounding and build state for the **Firmware Mission Control** repository.

## 1. System Environment
*   **Operating System:** Linux
*   **Active Workspace:** `/home/joe-mcu/m/nodemcu-esp8266`
*   **PlatformIO Core:** Version 6.2.0
*   **PlatformIO Executable:** `~/.local/bin/pio` (installed in `~/.platformio/penv`, symlinked to PATH)
*   **Python Version:** Python 3.12.3

## 2. Project Architecture & Configurations
The project contains a modular shared library under `lib/shared` and three firmware sketches in the `sketches` directory:
1.  **sensors** (`sketches/sensors`): DS18B20 temperature scanning, water analog probe sampling, and MQTT telemetry publishing.
2.  **water_heater** (`sketches/water_heater`): High-priority water heating control featuring async MQTT communication.
3.  **uhf_modulator** (`sketches/uhf_modulator`): Jitter-free high-frequency SSR controller with a 120Hz Timer1 ISR and Bresenham scheduling.

## 3. Migration Actions Completed
*   **Instruction File Migration:** Renamed the legacy `GEMINI.md` to `ANTIGRAVITY.md` as part of migrating the codebase context to the Antigravity assistant.
*   **Portability Fixes:** Modified the root `platformio.ini` to replace hardcoded, absolute user directory paths (`/home/qcjensen/nodemcu-esp8266/...`) with portable, relative paths (`sketches/sensors/data`, etc.).
*   **Build Validation:** Successfully validated full compilation of all three environments using PlatformIO:
    *   `sensors`: Built successfully.
    *   `water_heater`: Built successfully.
    *   `uhf_modulator`: Built successfully.
*   **Feature Switches Implementation & Zero-Probing Gating:** Added two runtime feature switches (`waterProbeEnabled` and `sensorNetworkEnabled`) with:
    *   Unified configuration options in `AppConfig` and endpoints (`/api/config/features`).
    *   **Strict Zero-Probing:** Gated `scanSensors()`, `requestTemperatureConversion()`, `readTemperatures()`, and `collectTemperatureResults()` in `lib/shared/sensor_bus.cpp` to halt physical 1-Wire bus activity when `sensorNetworkEnabled == false`. Gated `beginWaterSample()` and `updateWaterSample()` to halt ADC polling when `waterProbeEnabled == false`.
    *   **Heartbeat & Status Omission:** Device MQTT status heartbeats (`publishAggregateStatus`, `publishHeaterStatus`, `publishUhfStatus`) and `/api/status` payloads completely omit disabled fields (`sensorcount`, `simulated`, `networkdetected`, and `sensors` array when sensors are disabled; `water` object when water probe is disabled).
    *   **Endpoint Protection:** `/api/temps` and `/api/water` return `{"enabled": false}` when disabled. Manual trigger endpoints (`/api/sensors/scan`, `/api/water/sample`) reject requests with HTTP 400 when disabled.
    *   Web UIs: Greyed-out cards, clean disabled page states, and hotlinks to settings in the Web UIs.
    *   Command validation and error publishing over MQTT for disabled features.
    *   Prometheus metrics exclusion (lobotomization) when disabled.
    *   On-board OLED display integration (printing "- disabled -", "Sensors off", or "Sensors disabled" fallbacks on the status display screens when the relevant features are switched off).
*   **Water Level Probe v3 Upgrade:** Rewrote the analog level-detection code to support the simplified hardware schematic (`new water probe v3.jpg`):
    *   Removed legacy GPIO `PROBE_EN` / `D0` toggle control logic (`D0` is now unused).
    *   Implemented synchronous 3-conversion ADC averages on the `A0` input.
    *   Simplified sampling to run on a clean 15-second loop inside `updateWaterSample()`.
    *   Preserved existing threshold array mapping and classification bounds.
*   **1-Wire Failure Analysis & Field Diagnostics:** Investigated erratic temperature reporting on sensor `28E1242500000016` and authored [`DS18B20-FAILURE-ANALYSIS.md`](DS18B20-FAILURE-ANALYSIS.md) documenting counterfeit clone silicon fingerprints (serial numbers containing `00 00 00`), capacitive register degradation, and moisture ingress in waterproof stainless capsules.
*   **Deployment & OTA Best Practices:**
    *   **Filesystem Synchronization:** Verify and update LittleFS (`uploadfs` or `/update`) whenever firmware or web assets (`data/index.html`) are updated to ensure UI controls match firmware capabilities.
    *   **Routed Network OTA Flashing:** When flashing devices across routed subnets where ArduinoOTA (port 8266) times out, use the HTTP update server:
        *   Firmware: `curl -F "firmware=@.pio/build/<env>/firmware.bin" http://<ip>/update`
        *   Filesystem: `curl -F "filesystem=@.pio/build/<env>/littlefs.bin" http://<ip>/update`

## 4. Environment-Specific Commands
To run PlatformIO commands from the project root directory:
*   Build sensors: `pio run -e sensors`
*   Build heater: `pio run -e water_heater`
*   Build UHF modulator: `pio run -e uhf_modulator`
*   Build LittleFS image: `pio run -e <env> -t buildfs`
*   OTA deploy via HTTP: `curl -F "firmware=@.pio/build/<env>/firmware.bin" http://<ip>/update`
