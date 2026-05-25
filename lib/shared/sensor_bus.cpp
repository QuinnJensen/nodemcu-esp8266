// sensor_bus.cpp - shared 1-Wire DS18B20 driver
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include <math.h>
#include "app_state.h"
#include "sensor_names.h"
#include "display_ui.h"
#include "console_log.h"

static const char* fakeSensorNames[3]     = {"sensor1", "sensor2", "sensor3"};
static const char* fakeSensorAddresses[3] = {"28DEAD2BAD0001A1", "28DEAD2BAD0002B2", "28DEAD2BAD0003C3"};
static float       fakeSensorTempsC[3]    = {21.1f, 22.8f, 24.4f};

bool conversionPending = false;
unsigned long conversionRequestedMs = 0;

void initSensorBus() {
  ds.setWaitForConversion(false);
  ds.begin();

  // Log power mode to UDP to help debug pull-up/wiring issues
  bool parasitic = ds.isParasitePowerMode();
  remotePrintf("[1-WIRE] Bus initialized. Mode: %s\n", parasitic ? "Parasitic" : "Powered (3-wire)");
}

String defaultSensorNameForAddress(const DeviceAddress addr) {
  // Use last 5 hex digits of address (low nibble of addr[5], full addr[6], full addr[7])
  uint32_t low20 = ((uint32_t)(addr[5] & 0x0F) << 16) | ((uint32_t)addr[6] << 8) | addr[7];
  char buf[16];
  snprintf(buf, sizeof(buf), "sens%05X", low20);
  return String(buf);
}

static void clearFakeSensorEffects() {
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

static void loadFakeSensors() {
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
    if (useFakeSensors && !everHadPhysicalSensors) clearFakeSensorEffects();
    sensorNetworkDetected = true;
    everHadPhysicalSensors = true;
    useFakeSensors = false;

    // Only set resolution if count changed or forced to avoid bus noise
    bool countChanged = (found != sensorCount);
    sensorCount = found;
    for (uint8_t i = 0; i < found; i++) {
      memcpy(sensorAddresses[i], discovered[i], sizeof(DeviceAddress));
      sensorPresent[i] = true;
      if (countChanged || force) {
        ds.setResolution(sensorAddresses[i], 12);
      }
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

void requestTemperatureConversion() {
  if (useFakeSensors || sensorCount == 0) return;
  pulseSpinnerDot(900);
  flashBlueLed(30);
  ds.requestTemperatures();
  conversionPending = true;
  conversionRequestedMs = millis();
}

void collectTemperatureResults() {
  if (useFakeSensors || !conversionPending) return;
  conversionPending = false;
  uint8_t count = sensorCount;
  for (uint8_t i = 0; i < count; i++) {
    yield();
    if (!sensorPresent[i]) continue;

    float t = DEVICE_DISCONNECTED_C;
    uint8_t retries = 3;
    
    // Retry loop for robust reading in noisy environments
    while (retries > 0) {
      t = ds.getTempC(sensorAddresses[i]);
      if (t != DEVICE_DISCONNECTED_C && t > -50.0f && t < 130.0f) break;
      retries--;
      if (retries > 0) {
        oneWire.reset(); // Force a bus reset to clear potential noise
        delay(15);      // Short wait for line to pull high
        yield();
      }
    }

    if (t == DEVICE_DISCONNECTED_C || t < -50.0f || t > 130.0f) {
      remotePrintf("[1-WIRE] Read FAIL index=%d addr=%s after 3 tries\n", i, addressToString(sensorAddresses[i]).c_str());
      sensorTempsC[i] = NAN;
    } else {
      sensorTempsC[i] = t;
      sensorPresent[i] = true;
    }
  }
  lastSensorSampleMs = millis();
}

void readTemperatures() {
  if (useFakeSensors) return;
  pulseSpinnerDot(900);
  flashBlueLed(30);
  ds.requestTemperatures();
  unsigned long start = millis();
  while (millis() - start < 800) { yield(); }
  uint8_t count = sensorCount;
  for (uint8_t i = 0; i < count; i++) {
    yield();
    if (!sensorPresent[i]) continue;
    float t = ds.getTempC(sensorAddresses[i]);
    if (t == DEVICE_DISCONNECTED_C || t < -50.0f || t > 130.0f) {
      remotePrintf("[1-WIRE] Read FAIL (sync) index=%d addr=%s\n", i, addressToString(sensorAddresses[i]).c_str());
      sensorTempsC[i] = NAN;
    } else {
      sensorTempsC[i] = t;
      sensorPresent[i] = true;
    }
  }
}

void sampleSensors() {
  scanSensors();
  requestTemperatureConversion();
}

String sensorAddressString(uint8_t i) {
  if (useFakeSensors && i < 3) return String(fakeSensorAddresses[i]);
  return addressToString(sensorAddresses[i]);
}
#endif // SHARED_LIB_USE_ONEWIRE
