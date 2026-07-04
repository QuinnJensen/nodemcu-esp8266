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

// Asynchronous sampling state
static bool          isSampling     = false;
static unsigned long lastReadMs     = 0;
static uint32_t      adcAccumulator = 0;
static uint8_t       adcCount       = 0;

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

// Start the asynchronous sampling process (10 samples, spaced 20ms apart)
void beginWaterSample() {
  if (isSampling) return; // Already running
  
  isSampling     = true;
  adcAccumulator = 0;
  adcCount       = 0;
  lastReadMs     = 0;
  waterProbing   = true;
  
  if (config.ledEnabled) setBlueLed(true);
}

// Driven every loop tick. Performs non-blocking oversampling.
void updateWaterSample() {
  unsigned long now = millis();
  
  if (isSampling) {
    if (lastReadMs == 0 || now - lastReadMs >= 20) {
      uint16_t val = analogRead(A0);
      
      // Discard the first reading (index 0) to bypass the 5ms ADC cache.
      // Accumulate the subsequent 10 readings.
      if (adcCount > 0) {
        adcAccumulator += val;
      }
      
      adcCount++;
      lastReadMs = now;
      
      if (adcCount == 11) { // 1 discarded + 10 accumulated
        waterAdcRaw       = (uint16_t)(adcAccumulator / 10);
        waterLevelIndex   = classifyWaterLevel(waterAdcRaw);
        waterVoltage      = (float)waterAdcRaw * 3.3f / 1023.0f;
        waterValid        = true;
        waterProbePresent = true; // Constantly connected now
        lastWaterSampleMs = millis();
        
        if (config.ledEnabled) setBlueLed(false);
        isSampling   = false;
        waterProbing = false;
        
        publishWaterStatus();
      }
    }
  } else {
    // Schedule next sample every 15 seconds
    if (lastWaterSampleMs == 0 || now - lastWaterSampleMs >= 15000) {
      beginWaterSample();
    }
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
