#pragma once
#include <Arduino.h>

#define screenwidth 128
#define screenheight 64
#define oledreset -1
#define oledaddr 0x3C
#define i2csda D5
#define i2cscl D6
#define forceportalpin D3
#define oneWirePin D2
#define probeOnPin D0
#define blueLedPin D4

#define configfile "/config.json"
#define sensornamefile "/sensor_names.json"

#define sensorheartbeatintervalms 15000UL
#define sensorrescanintervalms 60000UL
#define aggregateheartbeatintervalms 60000UL
#define defaultwaterheartbeatintervalms 60000UL
#define watermeasurewindowms 5000UL
#define displayintervalms 250UL
#define mqttretryms 3000UL
#define portaltimeoutsec 180
#define startupreconfigcountdownms 10000UL
#define mqttbuffersize 2048
#define devicetz "MST7MDT,M3.2.0,M11.1.0"

#define maxsensors 6
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
