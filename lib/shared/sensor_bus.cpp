// sensor_bus.cpp - shared 1-Wire DS18B20 driver
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include <math.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_names.h"
#include "display_ui.h"

bool conversionPending = false;
unsigned long conversionRequestedMs = 0;

void initSensorBus() {
  ds.setWaitForConversion(false);
  ds.begin();
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
