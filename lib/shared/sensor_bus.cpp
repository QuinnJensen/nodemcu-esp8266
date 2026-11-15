// sensor_bus.cpp - shared 1-Wire DS18B20 driver
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include <math.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_names.h"
#include "display_ui.h"
#include "console_log.h"

bool conversionPending = false;
unsigned long conversionRequestedMs = 0;

static uint16_t measureMedianRiseUs() {
  uint16_t s[3];
  for (uint8_t i = 0; i < 3; i++) {
    s[i] = oneWire.measureBusRiseTimeUs();
    delayMicroseconds(50);
  }
  if (s[0] > s[1]) { uint16_t t = s[0]; s[0] = s[1]; s[1] = t; }
  if (s[1] > s[2]) { uint16_t t = s[1]; s[1] = s[2]; s[2] = t; }
  if (s[0] > s[1]) { uint16_t t = s[0]; s[0] = s[1]; s[1] = t; }
  return s[1];
}

static void calibrateOneWireBus() {
  bool hasPrev = false;
  uint16_t prevRise = 0xFFFF;
  uint8_t prevDelay = 10;
  uint16_t prevPresStart = 0xFFFF;

  if (LittleFS.exists(onewirecalfile)) {
    File f = LittleFS.open(onewirecalfile, "r");
    if (f) {
      StaticJsonDocument<256> doc;
      if (!deserializeJson(doc, f)) {
        hasPrev = true;
        prevRise = doc["rise_us"] | 0xFFFF;
        prevDelay = doc["read_delay_us"] | 10;
        prevPresStart = doc["pres_start_us"] | 0xFFFF;
      }
      f.close();
    }
  }

  uint16_t riseUs = measureMedianRiseUs();
  uint8_t resetOk = oneWire.reset();
  bool idleHigh = oneWire.getLastResetIdleHigh();
  bool drivenLowOk = oneWire.getLastResetDrivenLowOk();
  uint16_t presStart = oneWire.getLastPresenceStartUs();
  uint16_t presEnd = oneWire.getLastPresenceEndUs();

  uint8_t compDelay = 10;
  if (riseUs < 100) {
    uint16_t target = riseUs + 4;
    if (target < 10) target = 10;
    if (target > 22) target = 22;
    compDelay = (uint8_t)target;
  } else if (hasPrev && prevDelay >= 10 && prevDelay <= 22) {
    compDelay = prevDelay;
  } else {
    compDelay = 14;
  }

  oneWire.setReadSampleDelayUs(compDelay);

  bool changed = !hasPrev ||
                 (riseUs != prevRise && (riseUs >= 100 || prevRise >= 100 || abs((int)riseUs - (int)prevRise) >= 2)) ||
                 (compDelay != prevDelay) ||
                 (presStart != prevPresStart && (presStart == 0xFFFF || prevPresStart == 0xFFFF || abs((int)presStart - (int)prevPresStart) >= 6));

  auto fmtRise = [](uint16_t r) -> String {
    if (r == 0xFFFF) return "STUCK_LOW";
    if (r == 0xFFFE) return ">100us";
    if (r == 0xFFFD) return "STUCK_HIGH";
    return String(r) + "us";
  };

  String msg = "[1W-CAL] ";
  if (!hasPrev) {
    msg += "initial: ";
  } else if (changed) {
    msg += "CHANGED: ";
  } else {
    msg += "unchanged: ";
  }
  msg += "idle=" + String(idleHigh ? "HIGH" : "LOW");
  msg += " drivenLow=" + String(drivenLowOk ? "OK" : "FAIL");
  msg += " rise=" + fmtRise(riseUs);
  if (hasPrev && changed && prevRise != riseUs) {
    msg += "(was " + fmtRise(prevRise) + ")";
  }
  msg += " pres=" + String(resetOk ? (String(presStart) + ".." + String(presEnd) + "us") : "NONE");
  msg += " read_delay=" + String(compDelay) + "us";
  if (hasPrev && changed && prevDelay != compDelay) {
    msg += "(was " + String(prevDelay) + "us)";
  }
  consoleLog(changed && hasPrev ? CLOG_WARN : CLOG_INFO, msg);

  if (!hasPrev || changed) {
    File f = LittleFS.open(onewirecalfile, "w");
    if (f) {
      StaticJsonDocument<256> doc;
      doc["rise_us"] = riseUs;
      doc["read_delay_us"] = compDelay;
      doc["pres_start_us"] = presStart;
      doc["pres_end_us"] = presEnd;
      doc["reset_ok"] = (bool)resetOk;
      serializeJson(doc, f);
      f.close();
    }
  }
}

void initSensorBus() {
  oneWire.begin(oneWirePin);
  ds.setWaitForConversion(false);
  ds.begin();
  calibrateOneWireBus();
}

String defaultSensorNameForAddress(const DeviceAddress addr) {
  // Use last 5 hex digits of address (low nibble of addr[5], full addr[6], full addr[7])
  uint32_t low20 = ((uint32_t)(addr[5] & 0x0F) << 16) | ((uint32_t)addr[6] << 8) | addr[7];
  char buf[16];
  snprintf(buf, sizeof(buf), "sens%05X", low20);
  return String(buf);
}

void scanSensors(bool force) {
  if (!config.sensorNetworkEnabled) return;
  if (!force && lastSensorRescanMs > 0 && millis() - lastSensorRescanMs < sensorrescanintervalms) return;
  lastSensorRescanMs = millis();

  flashBlueLed(30);

  DeviceAddress discovered[maxsensors];
  bool duplicateFound = false;
  uint8_t found = 0;

  oneWire.reset_search();
  DeviceAddress addr;
  while (found < maxsensors && oneWire.search(addr)) {
    yield();
    if (!ds.validAddress(addr)) continue;
    bool seen = false;
    for (uint8_t i = 0; i < found; i++) {
      if (memcmp(discovered[i], addr, sizeof(DeviceAddress)) == 0) { seen = true; duplicateFound = true; break; }
    }
    if (seen) continue;
    memcpy(discovered[found], addr, sizeof(DeviceAddress));
    found++;
  }

  for (uint8_t i = 0; i < maxsensors; i++) {
    sensorPresent[i] = false;
    sensorTempsC[i] = NAN;
    memset(sensorAddresses[i], 0, sizeof(DeviceAddress));
  }

  if (found > 0) {
    sensorNetworkDetected = true;
    everHadPhysicalSensors = true;
    useFakeSensors = false;
    sensorCount = found;
    for (uint8_t i = 0; i < found; i++) {
      memcpy(sensorAddresses[i], discovered[i], sizeof(DeviceAddress));
      sensorPresent[i] = true;
      ds.setResolution(sensorAddresses[i], 12);
    }
    resolveSensorNamesFromAddresses();
    saveSensorNames();
    if (duplicateFound) setStatusMessage("1-wire dup skipped", 2000);
  } else {
    useFakeSensors = false;
    sensorCount = 0;
    setStatusMessage(sensorNetworkDetected ? "1-wire missing" : "no 1-wire", 2000);
  }
}

void requestTemperatureConversion() {
  if (!config.sensorNetworkEnabled) return;
  if (useFakeSensors || sensorCount == 0) return;
  pulseSpinnerDot(900);
  flashBlueLed(30);
  ds.requestTemperatures();
  conversionPending = true;
  conversionRequestedMs = millis();
}

void collectTemperatureResults() {
  if (!config.sensorNetworkEnabled) return;
  if (useFakeSensors || !conversionPending) return;
  conversionPending = false;
  for (uint8_t i = 0; i < sensorCount; i++) {
    yield();
    float t = ds.getTempC(sensorAddresses[i]);
    if (t == DEVICE_DISCONNECTED_C) {
      sensorTempsC[i] = NAN;
      sensorPresent[i] = false;
    } else {
      sensorTempsC[i] = t;
      sensorPresent[i] = true;
    }
  }
  lastSensorSampleMs = millis();
}

void readTemperatures() {
  if (!config.sensorNetworkEnabled) return;
  if (useFakeSensors) return;
  pulseSpinnerDot(900);
  flashBlueLed(30);
  ds.requestTemperatures();
  unsigned long start = millis();
  while (millis() - start < 800) { yield(); }
  for (uint8_t i = 0; i < sensorCount; i++) {
    yield();
    float t = ds.getTempC(sensorAddresses[i]);
    if (t == DEVICE_DISCONNECTED_C) {
      sensorTempsC[i] = NAN;
      sensorPresent[i] = false;
    } else {
      sensorTempsC[i] = t;
      sensorPresent[i] = true;
    }
  }
}

void sampleSensors() {
  if (!config.sensorNetworkEnabled) return;
  scanSensors();
  requestTemperatureConversion();
}

String sensorAddressString(uint8_t i) {
  return addressToString(sensorAddresses[i]);
}
#endif // SHARED_LIB_USE_ONEWIRE
