// water_probe.cpp
#include "water_probe.h"
#include "app_state.h"
#include "app_config.h"
#include "pins_and_constants.h"
#include "display_ui.h"
#include "mqtt_publish.h"

static const char* waterLevelLabelsLocal[waterlevelcount] = {
  ">40gal", "15-40gal", "5-15gal", "<5gal"
};

void initWaterProbePins() {
  pinMode(blueLedPin, OUTPUT);
}

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

// Perform a 80ms oversampling burst with outlier rejection (40 samples, 2ms spacing)
void beginWaterSample() {
  if (waterProbing) return; // Prevent re-entry
  waterProbing = true;

  if (config.ledEnabled) setBlueLed(true);

  // Discard first read to clear multiplexer state and allow voltage to settle
  analogRead(A0);
  delay(2);

  uint16_t samples[40];
  for (uint8_t i = 0; i < 40; i++) {
    samples[i] = analogRead(A0);
    delay(2);
  }

  // Sort samples in ascending order using insertion sort
  for (uint8_t i = 1; i < 40; i++) {
    uint16_t key = samples[i];
    int16_t j = (int16_t)i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  // Calculate the trimmed mean by discarding the lowest 10 and highest 10 samples
  uint32_t sum = 0;
  for (uint8_t i = 10; i < 30; i++) {
    sum += samples[i];
  }

  waterAdcRaw       = (uint16_t)(sum / 20);
  waterLevelIndex   = classifyWaterLevel(waterAdcRaw);
  waterVoltage      = (float)waterAdcRaw * 3.3f / 1023.0f;
  waterValid        = true;
  waterProbePresent = true; // Constantly connected now
  lastWaterSampleMs = millis();

  if (config.ledEnabled) setBlueLed(false);
  waterProbing = false;

  publishWaterStatus();
}

// Drive automatic sampling every 15 seconds
void updateWaterSample() {
  unsigned long now = millis();
  if (lastWaterSampleMs == 0 || now - lastWaterSampleMs >= 15000) {
    beginWaterSample();
  }
}

void appendWaterToJson(JsonDocument& doc) {
  JsonObject water = doc.createNestedObject("water");
  water["enabled"]             = true;
  water["heartbeatintervalms"] = config.waterHeartbeatIntervalMs;
  water["probe_present"]       = waterProbePresent;
  water["adc"]                 = waterAdcRaw;
  water["voltage"]             = waterVoltage;
  water["valid"]               = waterValid;
  water["levelindex"]          = waterLevelIndex;
  water["level"]               = waterLevelLabel(waterLevelIndex);
  JsonArray thresholds = water.createNestedArray("thresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);
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
