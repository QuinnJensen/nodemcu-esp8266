#pragma once
#include <Arduino.h>
#include <DallasTemperature.h>

void initSensorBus();
void scanSensors(bool force = false);
void readTemperatures();
void sampleSensors();
void loadFakeSensors();
void clearFakeSensorEffects();
String defaultSensorNameForAddress(const DeviceAddress addr);
