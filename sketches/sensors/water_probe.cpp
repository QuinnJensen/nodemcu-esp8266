// water_probe.cpp
#include "water_probe.h"
#include "app_state.h"
#include "app_config.h"
#include "pins_and_constants.h"
#include "display_ui.h"

static const char* waterLevelLabelsLocal[waterthresholdcount] = {"no_probe", ">40gal", "15-40gal", "5-15gal", "<5gal"};

void initWaterProbePins() {
  pinMode(probeOnPin, OUTPUT);
  pinMode(blueLedPin, OUTPUT);
  digitalWrite(probeOnPin, LOW);
}

// move your existing sampleWaterLevel() and appendWaterToJson() here

const char* waterLevelLabel(uint8_t idx) {
  if (idx < waterlevelcount) return waterLevelLabelsLocal[idx];
  return "unknown";
}

uint8_t classifyWaterLevel(uint16_t adc) {
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    if (adc <= config.waterThresholds[i]) return i;
  }
  return WATER_LT_5;
}

void sampleWaterLevel() {
  pinMode(probeOnPin, OUTPUT);
  pinMode(blueLedPin, OUTPUT);
  setBlueLed(true);
  digitalWrite(probeOnPin, HIGH);
  delay(25);

  uint16_t quickAdc = analogRead(A0);
  if (quickAdc <= config.waterThresholds[WATER_NO_PROBE]) {
    waterProbePresent = false;
    waterValid = true;
    waterAdcRaw = quickAdc;
    waterLevelIndex = WATER_NO_PROBE;
    lastWaterSampleMs = millis();
    digitalWrite(probeOnPin, LOW);
    setBlueLed(false);
    return;
  }

  waterProbePresent = true;
  uint32_t sum = 0;
  uint32_t totalSamples = 0;
  const uint16_t batchReads = 32;
  unsigned long start = millis();

  while (millis() - start < watermeasurewindowms) {
    for (uint16_t i = 0; i < batchReads; i++) {
      sum += analogRead(A0);
    }
    totalSamples += batchReads;
    delay(50);
  }

  digitalWrite(probeOnPin, LOW);
  setBlueLed(false);

  if (totalSamples == 0) totalSamples = 1;
  waterAdcRaw = (uint16_t)(sum / totalSamples);
  waterLevelIndex = classifyWaterLevel(waterAdcRaw);
  waterValid = true;
  lastWaterSampleMs = millis();
}

void appendWaterToJson(JsonDocument& doc) {
  JsonObject water = doc.createNestedObject("water");
  water["enabled"] = true;
  water["heartbeatintervalms"] = config.waterHeartbeatIntervalMs;
  water["probe_present"] = waterProbePresent;
  water["adc"] = waterAdcRaw;
  water["valid"] = waterValid;
  water["levelindex"] = waterLevelIndex;
  water["level"] = waterLevelLabel(waterLevelIndex);
  JsonArray thresholds = water.createNestedArray("thresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);
  water["noprobeadc"] = config.waterThresholds[WATER_NO_PROBE];
  if (lastWaterSampleMs > 0) water["sampleagems"] = millis() - lastWaterSampleMs;
}

bool updateWaterThresholdsFromJson(JsonVariantConst src) {
  if (!src.is<JsonArrayConst>()) return false;
  JsonArrayConst arr = src.as<JsonArrayConst>();
  if (arr.size() != waterthresholdcount) return false;

  uint16_t nextVals[waterthresholdcount];
  uint16_t prev = 0;
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    uint32_t v = arr[i] | 0;
    if (v > 1023) return false;
    nextVals[i] = (uint16_t)v;
    if (i > 0 && nextVals[i] < prev) return false;
    prev = nextVals[i];
  }
  for (uint8_t i = 0; i < waterthresholdcount; i++) config.waterThresholds[i] = nextVals[i];
  return true;
}
