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

void initSensorBus() {
  ds.setWaitForConversion(false);
  ds.begin();
}

String defaultSensorNameForAddress(const DeviceAddress addr) {
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
  uint8_t found = 0;

  oneWire.reset_search();
  DeviceAddress addr;
  while (found < maxsensors && oneWire.search(addr)) {
    yield();
    if (!ds.validAddress(addr)) continue;
    
    // Check CRC of the ROM address itself
    if (OneWire::crc8(addr, 7) != addr[7]) {
      remotePrintf("[1-WIRE] Scan CRC error on addr: %s\n", addressToString(addr).c_str());
      continue;
    }

    bool seen = false;
    for (uint8_t i = 0; i < found; i++) {
      if (memcmp(discovered[i], addr, sizeof(DeviceAddress)) == 0) { seen = true; break; }
    }
    if (seen) continue;
    memcpy(discovered[found], addr, sizeof(DeviceAddress));
    found++;
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
  } else if (!sensorNetworkDetected) {
    loadFakeSensors();
  } else {
    useFakeSensors = false;
    sensorCount = 0;
  }
}

void readTemperatures() {
  if (useFakeSensors || sensorCount == 0) return;
  
  pulseSpinnerDot(900);
  flashBlueLed(30);

  // 1. Trigger global conversion
  ds.requestTemperatures();
  
  // 2. Wait for completion (750ms for 12-bit, we wait 850ms for safety)
  unsigned long start = millis();
  while (millis() - start < 850) { yield(); }

  // 3. Read each sensor with CRC check
  for (uint8_t i = 0; i < sensorCount; i++) {
    yield();
    if (!sensorPresent[i]) continue;

    float t = NAN;
    uint8_t retries = 2;
    while (retries--) {
      t = ds.getTempC(sensorAddresses[i]);
      
      // The DallasTemperature library handles CRC internally during getTempC.
      // If it fails CRC or is missing, it returns DEVICE_DISCONNECTED_C (-127).
      // We also filter out 85.0C (Power-On-Reset value).
      if (t != DEVICE_DISCONNECTED_C && t != 85.0f && t > -50.0f && t < 130.0f) {
        sensorTempsC[i] = t;
        break;
      }
      
      if (retries > 0) {
        oneWire.reset();
        delay(10);
      } else {
        sensorTempsC[i] = NAN;
        remotePrintf("[1-WIRE] Read FAIL idx=%d addr=%s\n", i, addressToString(sensorAddresses[i]).c_str());
      }
    }
  }
  lastSensorSampleMs = millis();
}

void sampleSensors() {
  scanSensors();
  readTemperatures();
}

String sensorAddressString(uint8_t i) {
  if (useFakeSensors && i < 3) return String(fakeSensorAddresses[i]);
  return addressToString(sensorAddresses[i]);
}
#endif // SHARED_LIB_USE_ONEWIRE
