// water_probe.cpp
#include "water_probe.h"
#include "app_state.h"
#include "app_config.h"
#include "pins_and_constants.h"
#include "display_ui.h"
#include "mqtt_publish.h"
#include "sensor_bus.h"

static const char* waterLevelLabelsLocal[waterlevelcount] = {
  ">40gal", "15-40gal", "5-15gal", "<5gal"
};

// Filtering & Hysteresis parameters
static const uint8_t SAMPLE_COUNT = 16;
static const uint8_t HYSTERESIS = 5;              // ~16mV deadband around nominal thresholds
static const uint8_t OVERSIGHT_MAX_SAMPLES = 3;  // 3 consecutive cycles (45s) on other side to resign

static uint8_t oversightTarget = 255;
static uint8_t oversightCount = 0;

void initWaterProbePins() {
  pinMode(blueLedPin, OUTPUT);
}

const char* waterLevelLabel(uint8_t idx) {
  if (idx < waterlevelcount) return waterLevelLabelsLocal[idx];
  return "unknown";
}

// Pure nominal classification without hysteresis
static uint8_t classifyNominal(uint16_t adc) {
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    if (adc <= config.waterThresholds[i]) return i;
  }
  return WATER_LT_5;
}

// Hysteresis classification with temporal oversight:
// - Latches when entering a state with a +/- HYSTERESIS buffer.
// - If the averaged ADC consistently lands on the other side of the nominal
//   threshold for OVERSIGHT_MAX_SAMPLES consecutive cycles, it resets and resigns
//   to the nominal/prior level calculation.
uint8_t classifyWaterLevel(uint16_t adc) {
  uint8_t nominal = classifyNominal(adc);

  if (!waterValid || waterLevelIndex >= waterlevelcount) {
    oversightCount = 0;
    oversightTarget = 255;
    return nominal;
  }

  uint8_t k = waterLevelIndex;

  // Determine hysteresis bounds for the current state k
  // Note: higher water level = lower ADC; lower water level = higher ADC
  int32_t lowerBound = (k > 0) ? (int32_t)config.waterThresholds[k - 1] - HYSTERESIS : -1;
  int32_t upperBound = (k < waterthresholdcount) ? (int32_t)config.waterThresholds[k] + HYSTERESIS : 999999;

  // If adc broke beyond the hysteresis envelope, transition immediately
  if ((lowerBound >= 0 && adc < (uint16_t)lowerBound) || (adc > (uint16_t)upperBound)) {
    oversightCount = 0;
    oversightTarget = 255;
    return nominal;
  }

  // Inside the hysteresis envelope [lowerBound, upperBound]:
  // Check if the ADC has landed on the other side of the nominal threshold
  if (nominal != k) {
    if (nominal == oversightTarget) {
      oversightCount++;
      if (oversightCount >= OVERSIGHT_MAX_SAMPLES) {
        // Consistently landed on the other side of the threshold:
        // Reset oversight and resign to the nominal level calculation
        oversightCount = 0;
        oversightTarget = 255;
        return nominal;
      }
    } else {
      oversightTarget = nominal;
      oversightCount = 1;
    }
  } else {
    // ADC is on the same side as current state: clear oversight counter
    oversightCount = 0;
    oversightTarget = 255;
  }

  return k;
}

// 16-sample trimmed mean over a full 60Hz mains cycle + Exponential Moving Average (EMA)
void beginWaterSample() {
  if (!config.waterProbeEnabled) return;
  if (waterProbing) return; // Prevent re-entry
  waterProbing = true;

  if (config.ledEnabled) setBlueLed(true);

  // Discard first read to clear multiplexer state
  analogRead(A0);
  delayMicroseconds(500);

  // Collect 16 samples spaced ~1.1ms apart (~17.6ms window spans a full 60Hz cycle)
  uint16_t samples[SAMPLE_COUNT];
  for (uint8_t i = 0; i < SAMPLE_COUNT; i++) {
    samples[i] = analogRead(A0);
    delayMicroseconds(1100);
  }

  // Insertion sort
  for (uint8_t i = 1; i < SAMPLE_COUNT; i++) {
    uint16_t key = samples[i];
    int8_t j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  // Trimmed mean: discard 4 lowest and 4 highest outliers, average middle 8
  uint32_t sum = 0;
  for (uint8_t i = 4; i < 12; i++) {
    sum += samples[i];
  }
  uint16_t instantaneousAdc = (uint16_t)(sum / 8);

  // Store instantaneous aggregated values from this sample episode
  waterAdcLast = instantaneousAdc;
  waterVoltageLast = (float)waterAdcLast * 3.3f / 1023.0f;

  // Exponential Moving Average (alpha = 0.25: 25% new sample, 75% history)
  if (!waterValid || waterAdcRaw == 0) {
    waterAdcRaw = instantaneousAdc;
  } else {
    waterAdcRaw = (uint16_t)((instantaneousAdc + 3UL * waterAdcRaw) / 4UL);
  }

  waterLevelIndex   = classifyWaterLevel(waterAdcRaw);
  waterVoltage      = (float)waterAdcRaw * 3.3f / 1023.0f;
  waterValid        = true;
  waterProbePresent = true; // Constantly connected now
  lastWaterSampleMs = millis();

  if (config.ledEnabled) setBlueLed(false);
  waterProbing = false;

  publishWaterStatus();
}

// Drive automatic sampling every 15 seconds, avoiding in-flight 1-Wire conversions
void updateWaterSample() {
  if (!config.waterProbeEnabled) return;
  if (conversionPending) return; // Wait until 1-Wire bus conversion & collection finish

  unsigned long now = millis();
  if (lastWaterSampleMs == 0 || now - lastWaterSampleMs >= 15000) {
    beginWaterSample();
  }
}

void appendWaterToJson(JsonDocument& doc) {
  if (!config.waterProbeEnabled) return;
  JsonObject water = doc.createNestedObject("water");
  water["enabled"]             = true;
  water["heartbeatintervalms"] = config.waterHeartbeatIntervalMs;
  water["probe_present"]       = waterProbePresent;
  water["adc"]                 = waterAdcRaw;
  water["voltage"]             = waterVoltage;
  water["adc_last"]            = waterAdcLast;
  water["voltage_last"]        = waterVoltageLast;
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
  oversightCount = 0;
  oversightTarget = 255;
  waterLevelIndex = classifyNominal(waterAdcRaw);
  return true;
}
