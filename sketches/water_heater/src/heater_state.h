// heater_state.h - water heater controller state and Bresenham SSR modulator
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define WH_FULL_SCALE_WATTS 1800.0f
#define WH_NOMINAL_VRMS     120.0f
#define WH_CAL_POINTS       10
#define WH_MODULATOR_HZ     60UL

extern volatile uint8_t  isrPowerPct;
extern volatile uint8_t  isrOutputState;
extern volatile uint32_t simTickCount;
extern volatile uint32_t simOnTickCount;

extern int requestedPowerPct;
extern int priorPowerPct;
extern int displayedPowerWatts;
extern unsigned long lastCommandMs;
extern unsigned long powerLevelChangedMs;
extern float calTable[WH_CAL_POINTS];

void initHeaterIo();
void startModulator();
void serviceModulatorOneShot();
void clearCalibrationTable();
bool hasAnyCalibration();
bool loadCalibration();
bool saveCalibration();
bool purgeCalibrationFile();
float estimateCorrectedWatts(int pct);
float estimateCurrentAmps(float estWatts);
void  refreshDisplayedPower();
void  updatePowerValues(int pct);
bool  handleCalibrationRequest(float actualWatts);
void  appendHeaterStateToJson(JsonDocument& doc);
