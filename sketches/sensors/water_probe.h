// water_probe.h

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void initWaterProbePins();
void sampleWaterLevel();
uint8_t classifyWaterLevel(uint16_t adc);
const char* waterLevelLabel(uint8_t idx);
void appendWaterToJson(JsonDocument& doc);
bool updateWaterThresholdsFromJson(JsonVariantConst src);
