# uhf_modulator

NodeMCU ESP8266 sketch for a 433 MHz OOK/ASK RF transmitter.
Based on `uhf_modulator.ino`. **Not yet refactored** to use the
`esp8266-common` shared library — kept verbatim for a future refactor pass.

## Build

```sh
pio run -e uhf_modulator
pio run -e uhf_modulator -t upload
pio run -e uhf_modulator -t uploadfs
```

## Pin assignments

| Pin | GPIO | Function |
|-----|------|----------|
| D3 | 0 | RF DATA output (user-specified; also FLASH button — see note) |
| D4 | 2 | Onboard blue LED (active LOW, mirrored during TX) |
| D5 | 14 | I2C SDA (SSD1306 OLED) |
| D6 | 12 | I2C SCL (SSD1306 OLED) |

> **Note:** D3/GPIO0 is a boot-strap pin. Verify your transmitter DATA input
> is high-impedance so it does not pull GPIO0 LOW at boot.

## MQTT topics

```
<baseTopic>/<deviceId>/command
<baseTopic>/<deviceId>/status
<baseTopic>/<deviceId>/results
<baseTopic>/<deviceId>/tx
```

## Persistent files (LittleFS)

- `config.json` — MQTT, device ID, Prometheus port
- `profiles.json` — timing profile table (up to 16)
- `codes.json` — named code table (up to 32)

## Seeded defaults

| Profile | pulse | sync | zero | one | bits | repeat |
|---------|-------|------|------|-----|------|--------|
| proto1350us | 350 µs | 1×31 | 1×3 | 3×1 | 24 | 10 |
| proto1354uscompat | 354 µs | 1×31 | 1×3 | 3×1 | 24 | 25 |

| Code | Profile | Value | Bits | Repeat |
|------|---------|-------|------|--------|
| giandelon | proto1350us | 14199672 | 24 | 10 |
| giandeloff | proto1350us | 14199668 | 24 | 10 |
| heateron | proto1350us | 757795032 | 31 | 1 |
| heateroff | proto1350us | 757793412 | 31 | 1 |
| heaterplus | proto1350us | 757793912 | 31 | 1 |
| heaterminus | proto1350us | 757793092 | 31 | 1 |

## Shared patterns (future lib candidates)

- WiFiManager captive portal with startup countdown
- MQTT connect/publish/callback
- Prometheus metrics endpoint
- SSD1306 OLED spinner + status line
- LittleFS config/profiles/codes load/save
- `sanitizeTopicPart`, `ipToString`, `timeToString`
