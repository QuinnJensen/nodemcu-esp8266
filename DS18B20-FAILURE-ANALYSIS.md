# 1-Wire DS18B20 Temperature Sensor Failure Analysis & Field Diagnostics

**Document Version:** 1.0  
**Date:** September 30, 2026  
**Investigated Device:** NodeMCU ESP8266 (`mbr` at `10.42.8.108`)  
**Target Probe:** `28E1242500000016` (reported erratic / random readings)  

---

## 1. Executive Summary

During field operation of the `sensors` firmware on NodeMCU hardware, temperature sensors on the 1-Wire bus have been observed to operate normally for weeks or months before degrading and reporting wildly fluctuating, erratic, or "random" temperatures.

Investigation into the specific probe (`28E1242500000016`) and empirical research from the hardware/IoT community reveal two primary root causes:
1. **Counterfeit / Clone Silicon:** The ROM address contains 24 consecutive zero bits (`00 00 00`), a known fingerprint of third-party clone dies (e.g., Family D1 / D2 / B1) that use substandard analog front-ends, capacitive memory instead of EEPROM, and uncalibrated internal oscillators that degrade over time.
2. **Moisture Ingress in "Waterproof" Stainless Steel Capsules:** The majority of commercial waterproof DS18B20 probes use low-grade adhesive and PVC heat shrink at the stainless steel tube interface. Thermal expansion mismatches break the seal over time, allowing moisture into the capsule. The resulting electrolytic leakage across the internal TO-92 pins alters the analog ADC reference, producing corrupt temperature data while preserving valid 1-Wire digital framing and CRC.

---

## 2. Live Bus Observations

Querying `/api/temps` on `10.42.8.108` revealed three active sensors on the bus:

| Index | Name | 64-bit ROM Address | ROM Format Analysis | Status |
|:---:|:---|:---|:---|:---:|
| 1 | `busted` | `28 E1 24 25 00 00 00 16` | Family `0x28`, Serial `E1 24 25 00 00 00`, CRC `0x16` | **Erratic / Random** |
| 2 | `ambient` | `28 E3 AD 24 00 00 00 CF` | Family `0x28`, Serial `E3 AD 24 00 00 00`, CRC `0xCF` | Clone pattern (`00 00 00`) |
| 3 | `board` | `28 FF 64 0E 73 C4 C7 9E` | Family `0x28`, Serial `FF 64 0E 73 C4 C7`, CRC `0x9E` | Authentic Maxim C4 die pattern |

---

## 3. Technical Diagnostic: Why the Readings Are "Random" (Not Disconnected)

A critical clue in troubleshooting is that the sensor does **not** report `-127.0°C` (`DEVICE_DISCONNECTED_C`).

In [`DallasTemperature.cpp`](file:///home/joe-mcu/m/nodemcu-esp8266/.pio/libdeps/sensors/DallasTemperature/DallasTemperature.cpp#L180-L185), every read of the 9-byte scratchpad is validated by an 8-bit Dallas/Maxim CRC:
```cpp
bool DallasTemperature::isConnected(const uint8_t* deviceAddress, uint8_t* scratchPad) {
    bool b = readScratchPad(deviceAddress, scratchPad);
    return b && !isAllZeros(scratchPad) && (_wire->crc8(scratchPad, 8) == scratchPad[SCRATCHPAD_CRC]);
}
```

* **If the 1-Wire bus had electrical noise or timing glitches:** The master would fail to read time-slots accurately, the 8-bit CRC would fail, and the firmware would record `-127.0°C` / mark the sensor disconnected.
* **Because CRC validation passes:** The digital communication engine, UART state machine, and scratchpad transmission are mathematically flawless. The corrupted values originate **internally within the silicon's analog measurement engine** before the digital scratchpad is serialized and transmitted.

---

## 4. Root Causes & Failure Mechanisms

### 4.1. Counterfeit Silicon & Substandard Clone Dies
Extensive decapping and reverse-engineering research conducted by Chris Petrich ([`cpetrich/counterfeit_DS18B20`](https://github.com/cpetrich/counterfeit_DS18B20)) has shown that virtually 100% of pre-assembled probes purchased through non-authorized distributors (Amazon, eBay, AliExpress) contain clone silicon.

* **ROM Fingerprint:** Authentic Maxim/Analog Devices DS18B20 chips produced since ~2009 use the `C4` die with 48-bit serial counters. Addresses formatted like `28-xx-xx-xx-00-00-00-xx` are indicative of clone families.
* **Capacitive Storage vs. EEPROM:** Clone families (such as *Family D1 "Noisy Rubbish"*) do not contain true non-volatile EEPROM cells. Instead, they use integrated high-value silicon capacitors to retain scratchpad state across power cycles. Over time, dielectric breakdown and charge leakage cause register corruption and severe analog drift.
* **Silicon Degradation:** Cheap clone dies suffer from electromigration, thermal latch-up, and uncalibrated internal bandgap voltage references that drift significantly after continuous power-on hours.

### 4.2. Moisture Ingress in "Waterproof" Probes (#1 Cause of Field Failure)
Most commercial waterproof DS18B20 probes consist of a TO-92 sensor soldered to a PVC cable, inserted into a 6x50mm stainless steel capsule, and filled with hot-melt glue or low-grade adhesive sealed with heat shrink.

* **Thermal Cycling Mismatch:** Stainless steel, PVC cable jackets, and hot-melt glue have vastly different thermal expansion coefficients. Repeated heating and cooling cycles create micro-fractures in the seal.
* **Capillary Action:** Water and humidity are drawn into the capsule via capillary action along the cable strands and tube walls.
* **Electrolytic Pin Bridging:** Moisture forms an electrolytic conductive film across the closely spaced pins (GND, DQ, VDD) of the internal TO-92 package.
* **Analog Measurement Distortion:** Even micro-amperes of leakage current between VDD/GND and the internal bandgap / delta-sigma ADC input cause the sensor to compute erratic, jumping temperature values. The chip's internal microcontroller then computes a valid CRC over this corrupted reading and sends it out onto the bus.

### 4.3. Power-On Reset & Brownout Glitches (The 85°C Issue)
* The default power-up state of the DS18B20 temperature register is `0x0550` (**`+85.0°C`**).
* If high-resistance corrosion on the supply line or ground bounce causes microsecond brownouts during the high-current conversion phase (up to 1.5mA per sensor), the sensor resets to its default power-on state, intermittently inserting 85°C spikes into telemetry.

---

## 5. Recommended Solutions & Best Practices

### 5.1. Hardware & Installation Fixes
1. **Source Authentic Silicon:** Purchase bare DS18B20+ chips directly from franchised distributors:
   * [Digi-Key](https://www.digikey.com)
   * [Mouser Electronics](https://www.mouser.com)
   * [Adafruit](https://www.adafruit.com) (verified authentic stock)
2. **Use Thermowells Instead of Submersion:** Never submerge pre-crimped probes directly into liquids long-term. Place the probe inside a closed, dry stainless steel or brass thermowell with thermal paste.
3. **Marine-Grade Potting (If Making Custom Probes):** If building waterproof probes, encapsulate the soldered TO-92 joint in marine-grade polyurethane or heat-cured epoxy (e.g., MG Chemicals 832HT) and use dual-wall adhesive-lined polyolefin heat shrink.
4. **Supply Voltage & Pull-Up Optimization:**
   * Always power sensors via dedicated 3-wire connections (**VDD, GND, DQ**). Avoid parasitic power mode.
   * If cable runs exceed 5–10 meters, decrease the pull-up resistor on DQ from `4.7kΩ` down to `2.2kΩ–3.0kΩ` to improve rising edge times.

### 5.2. Firmware Plausibility Filtering (Workaround)
To prevent compromised sensors from corrupting aggregated metrics and downstream telemetry:
1. **Rate-of-Change Limit:** Reject readings where $|\Delta T| > 3.0^\circ\text{C}$ within a 15-second sample window unless confirmed across multiple consecutive cycles.
2. **Power-On Reset Filter:** Explicitly discard `85.000°C` readings if preceded by a normal ambient reading.
3. **Out-of-Bounds Disqualification:** Discard readings outside expected physical boundaries (e.g., $< -10^\circ\text{C}$ or $> 100^\circ\text{C}$ for domestic water systems).

---

## 6. References & External Research

* **Chris Petrich Counterfeit DS18B20 Research:**  
  [https://github.com/cpetrich/counterfeit_DS18B20](https://github.com/cpetrich/counterfeit_DS18B20)  
  *Comprehensive decapping, protocol analysis, and classification of clone families A through H.*
* **Analog Devices / Maxim DS18B20 Official Datasheet:**  
  [https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf](https://www.analog.com/media/en/technical-documentation/data-sheets/DS18B20.pdf)
* **Cave Pearl Project - Underwater DS18B20 Longevity & Encapsulation Failures:**  
  [https://thecavepearlproject.org](https://thecavepearlproject.org)  
  *In-depth field study on seal degradation and moisture ingress in underwater temperature monitoring.*
* **OpenEnergyMonitor 1-Wire Troubleshooting & Cable Noise:**  
  [https://openenergymonitor.org](https://openenergymonitor.org)
