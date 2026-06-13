# Project System State - Firmware Mission Control

This file maintains the active grounding and build state for the **Firmware Mission Control** repository.

## 1. System Environment
*   **Operating System:** Linux
*   **Active Workspace:** `/home/joe/nodemcu-esp8266`
*   **PlatformIO Core:** Version 6.1.19
*   **PlatformIO Executable:** `/home/joe/pio_venv/bin/pio`
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
    *   `sensors`: Built successfully in 2m 55s.
    *   `water_heater`: Built successfully in 2m 21s.
    *   `uhf_modulator`: Built successfully in 2m 26s.

## 4. Environment-Specific Commands
To run PlatformIO commands from the root directory using the local python virtual environment:
*   Build sensors: `/home/joe/pio_venv/bin/pio run -e sensors`
*   Build heater: `/home/joe/pio_venv/bin/pio run -e water_heater`
*   Build UHF modulator: `/home/joe/pio_venv/bin/pio run -e uhf_modulator`
