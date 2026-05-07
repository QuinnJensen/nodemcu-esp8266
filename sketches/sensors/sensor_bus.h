#pragma once
#include <Arduino.h>

void initSensorBus();
void scanSensors(bool force = false);
void requestTemperatureConversion();  // async: fires conversion, returns immediately
void collectTemperatureResults();     // async: reads results (call 800ms after request)
void readTemperatures();              // legacy blocking shim (feeds WDT safely)
void sampleSensors();                 // scan + async-safe read
String sensorAddressString(uint8_t i);

extern bool conversionPending;
extern unsigned long conversionRequestedMs;
