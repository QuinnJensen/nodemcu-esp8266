// water_probe.cpp
#include "water_probe.h"
#include "app_state.h"
#include "app_config.h"
#include "pins_and_constants.h"
#include "display_ui.h"
#include "mqtt_publish.h"

static const char* waterLevelLabelsLocal[waterthresholdcount] = {
  "no_probe", ">40gal", "15-40gal", "5-15gal", "<5gal"
};

enum WaterProbeState {
  WPS_IDLE,
  WPS_STABILIZE,   // probe pin HIGH, waiting 25 ms settle
  WPS_SAMPLE,      // accumulating ADC readings for watermeasurewindowms
  WPS_FINISH       // window done, compute result
};

static WaterProbeState probeState  = WPS_IDLE;
static unsigned long   phaseStart  = 0;
static uint32_t        adcSum      = 0;
static uint32_t        adcSamples  = 0;
static bool            quickDone   = false;  // set after no-probe early check

void initWaterProbePins() {
  pinMode(probeOnPin, OUTPUT);
  pinMode(blueLedPin, OUTPUT);
  digitalWrite(probeOnPin, LOW);
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

// Called by scheduler / web handler to request a new sample.
void beginWaterSample() {
  if (probeState != WPS_IDLE) return;   // already running
  adcSum      = 0;
  adcSamples  = 0;
  quickDone   = false;
  waterProbing = true;
  if (config.ledEnabled) setBlueLed(true);
  digitalWrite(probeOnPin, HIGH);
  phaseStart  = millis();
  probeState  = WPS_STABILIZE;
}

// Called every loop() tick.  All work is time-sliced; no delay() anywhere.
void updateWaterSample() {
  if (probeState == WPS_IDLE) return;

  unsigned long now = millis();

  // Keep the spinner center dot alive every tick while probing
  pulseSpinnerDot(displayintervalms * 2);

  if (probeState == WPS_STABILIZE) {
    if (now - phaseStart < 25) return;   // wait 25 ms for probe to settle

    // Quick no-probe check on first ADC read
    uint16_t q = analogRead(A0);
    if (q <= config.waterThresholds[WATER_NO_PROBE]) {
      // No probe -- still hold indicators for the full window so the
      // LED duration is consistent and distinguishable.
      waterProbePresent = false;
      waterAdcRaw       = q;
      waterLevelIndex   = WATER_NO_PROBE;
      quickDone         = true;
      // Don't accumulate; jump straight to the timed window so the
      // full 5-second LED/dot is visible.
      phaseStart = now;
      probeState = WPS_SAMPLE;
      return;
    }

    // Probe present -- begin accumulation window
    waterProbePresent = true;
    adcSum     += q;
    adcSamples  = 1;
    phaseStart  = now;
    probeState  = WPS_SAMPLE;
    return;
  }

  if (probeState == WPS_SAMPLE) {
    if (now - phaseStart < watermeasurewindowms) {
      // Accumulate a small batch each tick (no delay)
      if (!quickDone) {
        for (uint8_t i = 0; i < 8; i++) {
          adcSum += analogRead(A0);
        }
        adcSamples += 8;
      }
      return;
    }
    // Window expired -- fall through to finish
    probeState = WPS_FINISH;
  }

  if (probeState == WPS_FINISH) {
    digitalWrite(probeOnPin, LOW);
    if (config.ledEnabled) setBlueLed(false);

    if (!quickDone) {
      if (adcSamples == 0) adcSamples = 1;
      waterAdcRaw     = (uint16_t)(adcSum / adcSamples);
      waterLevelIndex = classifyWaterLevel(waterAdcRaw);
    }
    waterVoltage      = (float)waterAdcRaw * 3.3f / 1023.0f;
    waterValid        = true;
    lastWaterSampleMs = millis();
    waterProbing      = false;
    probeState        = WPS_IDLE;

    // Publish result now that the sample is complete
    publishWaterStatus();
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
