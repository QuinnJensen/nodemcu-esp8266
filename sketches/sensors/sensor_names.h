#pragma once
#include <Arduino.h>
#include <DallasTemperature.h>

bool loadSensorNames();
bool saveSensorNames();
bool isValidSensorName(const char* name);
bool setSensorNameByIndex(uint8_t index1, const char* newName);
bool setSensorNameByAddress(const char* address, const char* newName);
void resolveSensorNamesFromAddresses();
String addressToString(const DeviceAddress addr);
String addressSlice(const DeviceAddress addr);
String sensorAddressString(uint8_t i);
