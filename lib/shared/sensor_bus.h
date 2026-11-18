#pragma once
#include <Arduino.h>
#ifdef SHARED_LIB_USE_ONEWIRE
#include <DallasTemperature.h>

void initSensorBus();
void scanSensors(bool force = false);
void requestTemperatureConversion();       // async: fires conversion, returns immediately
void collectTemperatureResults();          // async: reads results (call 800ms after request)
void readTemperatures();                   // blocking shim for setup(), feeds WDT safely
void sampleSensors();                      // scan + async-safe read
String sensorAddressString(uint8_t i);
String defaultSensorNameForAddress(const DeviceAddress addr);

struct OneWireBootDiag {
  bool measured = false;
  uint16_t riseSamplesUs[3] = {0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t riseMedianUs = 0xFFFF;
  bool idleHigh = false;
  bool drivenLowOk = false;
  bool resetOk = false;
  uint16_t presStartUs = 0xFFFF;
  uint16_t presEndUs = 0xFFFF;
  uint8_t readDelayUs = 10;
};

extern OneWireBootDiag oneWireBootDiag;
extern bool conversionPending;
extern unsigned long conversionRequestedMs;
#endif
