// sensor_bus.cpp
#include "sensor_bus.h"
#include <math.h>
#include "app_state.h"
#include "sensor_names.h"
#include "display_ui.h"

static const char* fakeSensorNames[3] = {"sensor1", "sensor2", "sensor3"};
static const char* fakeSensorAddresses[3] = {"28DEAD2BAD0001A1", "28DEAD2BAD0002B2", "28DEAD2BAD0003C3"};
static float fakeSensorTempsC[3] = {21.1f, 22.8f, 24.4f};

void initSensorBus() {
  ds.begin();
}

// move your current clearFakeSensorEffects(), loadFakeSensors(),
// scanSensors(), readTemperatures(), sampleSensors() here with minimal edits

String defaultSensorNameForAddress(const DeviceAddress addr) {
  uint16_t low12 = ((uint16_t)(addr[6] & 0x0F) << 8) | addr[7];
  char buf[16];
  snprintf(buf, sizeof(buf), "sens%03X", low12);
  return String(buf);
}

void clearFakeSensorEffects() {
  useFakeSensors = false;
  for (uint8_t i = 0; i < maxsensors; i++) {
    sensorPresent[i] = false;
    sensorTempsC[i] = NAN;
    sensorNames[i][0] = 0;
    memset(sensorAddresses[i], 0, sizeof(DeviceAddress));
  }
  sensorCount = 0;
  displayStartSensor = 0;
}

void loadFakeSensors() {
  useFakeSensors = true;
  sensorCount = 3;
  for (uint8_t i = 0; i < maxsensors; i++) {
    sensorPresent[i] = false;
    sensorTempsC[i] = NAN;
    sensorNames[i][0] = 0;
    memset(sensorAddresses[i], 0, sizeof(DeviceAddress));
  }
  for (uint8_t i = 0; i < 3; i++) {
    sensorPresent[i] = true;
    sensorTempsC[i] = fakeSensorTempsC[i];
    strlcpy(sensorNames[i], fakeSensorNames[i], sensornamelen);
  }
}

void scanSensors(bool force) {
  if (!force && lastSensorRescanMs > 0 && millis() - lastSensorRescanMs < sensorrescanintervalms) return;
  lastSensorRescanMs = millis();
  DeviceAddress discovered[maxsensors];
  bool duplicateFound = false;
  uint8_t found = 0;

  oneWire.reset_search();
  DeviceAddress addr;
  while (found < maxsensors && oneWire.search(addr)) {
    if (!ds.validAddress(addr)) continue;

    bool seen = false;
    for (uint8_t i = 0; i < found; i++) {
      if (memcmp(discovered[i], addr, sizeof(DeviceAddress)) == 0) {
        seen = true;
        duplicateFound = true;
        break;
      }
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
    if (useFakeSensors && !everHadPhysicalSensors) clearFakeSensorEffects();
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
  } else if (!sensorNetworkDetected) {
    loadFakeSensors();
  } else {
    useFakeSensors = false;
    sensorCount = 0;
    setStatusMessage("1-wire missing", 2000);
  }
}

void readTemperatures() {
  if (useFakeSensors) return;
  ds.requestTemperatures();
  for (uint8_t i = 0; i < sensorCount; i++) {
    flashBlueLed(20);
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
  scanSensors();
  readTemperatures();
  lastSensorSampleMs = millis();
}

String sensorAddressString(uint8_t i) {
  if (useFakeSensors && i < 3) return String(fakeSensorAddresses[i]);
  return addressToString(sensorAddresses[i]);
}
