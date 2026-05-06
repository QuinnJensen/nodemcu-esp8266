#include "sensor_names.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "util.h"

bool loadSensorNames() {
  sensorNameRecordCount = 0;
  if (!LittleFS.exists(sensornamefile)) return false;
  File f = LittleFS.open(sensornamefile, "r");
  if (!f) return false;
  StaticJsonDocument<768> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  JsonArray arr = doc["sensors"].as<JsonArray>();
  if (arr.isNull()) return false;
  for (JsonObject obj : arr) {
    if (sensorNameRecordCount >= maxsensors) break;
    strlcpy(sensorNameRecords[sensorNameRecordCount].address, obj["address"] | "", sizeof(sensorNameRecords[sensorNameRecordCount].address));
    strlcpy(sensorNameRecords[sensorNameRecordCount].name, obj["name"] | "", sizeof(sensorNameRecords[sensorNameRecordCount].name));
    sensorNameRecordCount++;
  }
  return true;
}

bool saveSensorNames() {
  if (useFakeSensors) return false;

  for (uint8_t i = 0; i < sensorCount; i++) {
    String addr = addressToString(sensorAddresses[i]);
    bool found = false;
    for (uint8_t j = 0; j < sensorNameRecordCount; j++) {
      if (addr.equalsIgnoreCase(sensorNameRecords[j].address)) {
        strlcpy(sensorNameRecords[j].name, sensorNames[i], sizeof(sensorNameRecords[j].name));
        found = true;
        break;
      }
    }
    if (!found && sensorNameRecordCount < maxsensors) {
      strlcpy(sensorNameRecords[sensorNameRecordCount].address, addr.c_str(), sizeof(sensorNameRecords[sensorNameRecordCount].address));
      strlcpy(sensorNameRecords[sensorNameRecordCount].name, sensorNames[i], sizeof(sensorNameRecords[sensorNameRecordCount].name));
      sensorNameRecordCount++;
    }
  }

  StaticJsonDocument<768> doc;
  JsonArray arr = doc.createNestedArray("sensors");
  for (uint8_t j = 0; j < sensorNameRecordCount; j++) {
    if (!sensorNameRecords[j].address[0]) continue;
    JsonObject s = arr.createNestedObject();
    s["address"] = sensorNameRecords[j].address;
    s["name"] = sensorNameRecords[j].name;
  }
  File f = LittleFS.open(sensornamefile, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool isValidSensorName(const char* name) {
  if (!name || !name[0]) return false;
  size_t n = strlen(name);
  if (n >= sensornamelen) return false;
  for (size_t i = 0; i < n; i++) {
    char c = name[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-' || c == '.')) return false;
  }
  return true;
}

bool setSensorNameByIndex(uint8_t index1, const char* newName) {
  if (useFakeSensors) return false;
  if (index1 == 0 || index1 > sensorCount) return false;
  if (!isValidSensorName(newName)) return false;
  String addr = addressToString(sensorAddresses[index1 - 1]);
  return setSensorNameByAddress(addr.c_str(), newName);
}

bool setSensorNameByAddress(const char* address, const char* newName) {
  if (useFakeSensors) return false;
  if (!address || !address[0]) return false;
  if (!isValidSensorName(newName)) return false;

  bool activeFound = false;
  for (uint8_t i = 0; i < sensorCount; i++) {
    String addr = addressToString(sensorAddresses[i]);
    if (addr.equalsIgnoreCase(address)) {
      strlcpy(sensorNames[i], newName, sensornamelen);
      activeFound = true;
      break;
    }
  }

  bool recordFound = false;
  for (uint8_t j = 0; j < sensorNameRecordCount; j++) {
    if (String(sensorNameRecords[j].address).equalsIgnoreCase(address)) {
      strlcpy(sensorNameRecords[j].name, newName, sizeof(sensorNameRecords[j].name));
      recordFound = true;
      break;
    }
  }
  if (!recordFound) {
    if (sensorNameRecordCount >= maxsensors) return false;
    strlcpy(sensorNameRecords[sensorNameRecordCount].address, address, sizeof(sensorNameRecords[sensorNameRecordCount].address));
    strlcpy(sensorNameRecords[sensorNameRecordCount].name, newName, sizeof(sensorNameRecords[sensorNameRecordCount].name));
    sensorNameRecordCount++;
  }

  return activeFound && saveSensorNames();
}

void resolveSensorNamesFromAddresses() {
  for (uint8_t i = 0; i < sensorCount; i++) {
    sensorNames[i][0] = 0;
    String addr = addressToString(sensorAddresses[i]);
    for (uint8_t j = 0; j < sensorNameRecordCount; j++) {
      if (addr.equalsIgnoreCase(sensorNameRecords[j].address)) {
        strlcpy(sensorNames[i], sensorNameRecords[j].name, sensornamelen);
        break;
      }
    }
    if (!sensorNames[i][0]) {
      String def = defaultSensorNameForAddress(sensorAddresses[i]);
      strlcpy(sensorNames[i], def.c_str(), sensornamelen);
    }
  }
}

String addressToString(const DeviceAddress addr) {
  char buf[17];
  for (uint8_t i = 0; i < 8; i++) snprintf(buf + (i * 2), 3, "%02X", addr[i]);
  buf[16] = 0;
  return String(buf);
}

String addressSlice(const DeviceAddress addr) {
  String full = addressToString(addr);
  return full.substring(10);
}
