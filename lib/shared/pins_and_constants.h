#pragma once
#include <Arduino.h>

// ── OLED / I2C ───────────────────────────────────────────────────────────────
#define screenwidth 128
#define screenheight 64
#define oledreset -1
#define oledaddr 0x3C
#ifndef i2csda
#define i2csda D5
#endif
#ifndef i2cscl
#define i2cscl D6
#endif

// ── Common pins ──────────────────────────────────────────────────────────────
#ifndef forceportalpin
#define forceportalpin D3
#endif
#ifndef blueLedPin
#define blueLedPin D4
#endif

// ── 1-Wire (sensors sketch) ──────────────────────────────────────────────────
#ifndef oneWirePin
#define oneWirePin D2
#endif

// ── Water probe (sensors sketch only) ────────────────────────────────────────
#ifndef probeOnPin
#define probeOnPin D0
#endif

// ── LittleFS file paths ──────────────────────────────────────────────────────
#define configfile "/config.json"
#define sensornamefile "/sensor_names.json"

// ── Timing constants ─────────────────────────────────────────────────────────
#define sensorheartbeatintervalms 15000UL
#define sensorrescanintervalms 60000UL
#define aggregateheartbeatintervalms 60000UL
#define defaultwaterheartbeatintervalms 60000UL
#define watermeasurewindowms 5000UL
#define displayintervalms 250UL
#define mqttretryms 3000UL
#define portaltimeoutsec 180
#define startupreconfigcountdownms 10000UL
#define mqttbuffersize 4096
#define devicetz "MDT7MST,M3.2.0,M11.1.0"

#define maxsensors 10
#define sensornamelen 32
#define waterlevelcount 5
#define waterthresholdcount 5

enum WaterLevelIndex {
  WATER_NO_PROBE = 0,
  WATER_GT_40 = 1,
  WATER_15_TO_40 = 2,
  WATER_5_TO_15 = 3,
  WATER_LT_5 = 4,
  WATER_UNKNOWN = 5
};
