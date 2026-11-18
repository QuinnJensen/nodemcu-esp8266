// sensor_bus.cpp - shared 1-Wire DS18B20 driver
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include <math.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_names.h"
#include "display_ui.h"
#include "console_log.h"

static const char* fakeSensorNames[3]     = {"sensor1", "sensor2", "sensor3"};
static const char* fakeSensorAddresses[3] = {"28DEAD2BAD0001A1", "28DEAD2BAD0002B2", "28DEAD2BAD0003C3"};
static float       fakeSensorTempsC[3]    = {21.1f, 22.8f, 24.4f};

OneWireBootDiag oneWireBootDiag;
bool conversionPending = false;
unsigned long conversionRequestedMs = 0;

static String formatAddrHex(const DeviceAddress addr) {
  char buf[17];
  for (uint8_t i = 0; i < 8; i++) {
    snprintf(buf + i * 2, 3, "%02X", addr[i]);
  }
  buf[16] = '\0';
  return String(buf);
}

static uint16_t measureMedianRiseUs(uint16_t rawSamples[3]) {
  uint16_t s[3];
  for (uint8_t i = 0; i < 3; i++) {
    s[i] = oneWire.measureBusRiseTimeUs();
    rawSamples[i] = s[i];
    delayMicroseconds(50);
  }
  // Sort 3 values
  if (s[0] > s[1]) { uint16_t t = s[0]; s[0] = s[1]; s[1] = t; }
  if (s[1] > s[2]) { uint16_t t = s[1]; s[1] = s[2]; s[2] = t; }
  if (s[0] > s[1]) { uint16_t t = s[0]; s[0] = s[1]; s[1] = t; }
  return s[1];
}

static void calibrateOneWireBus() {
  bool hasPrev = false;
  uint16_t prevRise = 0xFFFF;
  uint8_t prevDelay = 10;
  uint16_t prevPresStart = 0xFFFF;
  uint16_t prevPresEnd = 0xFFFF;

  if (LittleFS.exists(onewirecalfile)) {
    File f = LittleFS.open(onewirecalfile, "r");
    if (f) {
      StaticJsonDocument<256> doc;
      if (!deserializeJson(doc, f)) {
        hasPrev = true;
        prevRise = doc["rise_us"] | 0xFFFF;
        prevDelay = doc["read_delay_us"] | 10;
        prevPresStart = doc["pres_start_us"] | 0xFFFF;
        prevPresEnd = doc["pres_end_us"] | 0xFFFF;
      }
      f.close();
    }
  }

  uint16_t riseSamples[3] = {0xFFFF, 0xFFFF, 0xFFFF};
  uint16_t riseUs = measureMedianRiseUs(riseSamples);
  uint8_t resetOk = oneWire.reset();
  bool idleHigh = oneWire.getLastResetIdleHigh();
  bool drivenLowOk = oneWire.getLastResetDrivenLowOk();
  uint16_t presStart = oneWire.getLastPresenceStartUs();
  uint16_t presEnd = oneWire.getLastPresenceEndUs();

  uint8_t compDelay = 10;
  if (riseUs < 100) {
    uint16_t target = riseUs + 4;
    if (target < 10) target = 10;
    if (target > 22) target = 22;
    compDelay = (uint8_t)target;
  } else if (hasPrev && prevDelay >= 10 && prevDelay <= 22) {
    compDelay = prevDelay;
  } else {
    compDelay = 14;
  }

  oneWire.setReadSampleDelayUs(compDelay);

  oneWireBootDiag.measured = true;
  for (uint8_t i = 0; i < 3; i++) oneWireBootDiag.riseSamplesUs[i] = riseSamples[i];
  oneWireBootDiag.riseMedianUs = riseUs;
  oneWireBootDiag.idleHigh = idleHigh;
  oneWireBootDiag.drivenLowOk = drivenLowOk;
  oneWireBootDiag.resetOk = (bool)resetOk;
  oneWireBootDiag.presStartUs = presStart;
  oneWireBootDiag.presEndUs = presEnd;
  oneWireBootDiag.readDelayUs = compDelay;

  bool changed = !hasPrev ||
                 (riseUs != prevRise && (riseUs >= 100 || prevRise >= 100 || abs((int)riseUs - (int)prevRise) >= 2)) ||
                 (compDelay != prevDelay) ||
                 (presStart != prevPresStart && (presStart == 0xFFFF || prevPresStart == 0xFFFF || abs((int)presStart - (int)prevPresStart) >= 6));

  String msg = "[1W-CAL] ";
  if (!hasPrev) {
    msg += "initial: ";
  } else if (changed) {
    msg += "CHANGED: ";
  } else {
    msg += "unchanged: ";
  }
  auto fmtRise = [](uint16_t r) -> String {
    if (r == 0xFFFF) return "STUCK_LOW";
    if (r == 0xFFFE) return ">100us";
    if (r == 0xFFFD) return "STUCK_HIGH";
    return String(r) + "us";
  };
  msg += "idle=" + String(idleHigh ? "HIGH" : "LOW");
  msg += " drivenLow=" + String(drivenLowOk ? "OK" : "FAIL");
  msg += " rise=" + fmtRise(riseUs);
  if (hasPrev && changed && prevRise != riseUs) {
    msg += "(was " + fmtRise(prevRise) + ")";
  }
  msg += " pres=" + String(resetOk ? (String(presStart) + ".." + String(presEnd) + "us") : "NONE");
  msg += " read_delay=" + String(compDelay) + "us";
  if (hasPrev && changed && prevDelay != compDelay) {
    msg += "(was " + String(prevDelay) + "us)";
  }
  consoleLog(changed && hasPrev ? CLOG_WARN : CLOG_INFO, msg);

  if (!hasPrev || changed) {
    File f = LittleFS.open(onewirecalfile, "w");
    if (f) {
      StaticJsonDocument<256> doc;
      doc["rise_us"] = riseUs;
      doc["read_delay_us"] = compDelay;
      doc["pres_start_us"] = presStart;
      doc["pres_end_us"] = presEnd;
      doc["reset_ok"] = (bool)resetOk;
      serializeJson(doc, f);
      f.close();
    }
  }
}

static void runGpioStartupDiag() {
  const uint8_t p = oneWirePin;
  const uint32_t mask = (1UL << p);

  uint32_t gpf0 = GPF(p);
  uint32_t gpc0 = GPC(p);
  uint8_t gpe0 = (GPE & mask) ? 1 : 0;
  uint8_t gpo0 = (GPO & mask) ? 1 : 0;
  uint8_t gpi0 = (GPI & mask) ? 1 : 0;
  consoleLog(CLOG_INFO, String("[1W-GPIO] init D2(GPIO") + p + ") GPF=0x" + String(gpf0, HEX) +
             " GPC=0x" + String(gpc0, HEX) + " GPE=" + gpe0 + " GPO=" + gpo0 + " GPI=" + gpi0);

  pinMode(p, INPUT);
  delayMicroseconds(20);
  int inVal = digitalRead(p);

  pinMode(p, INPUT_PULLUP);
  delayMicroseconds(20);
  int puVal = digitalRead(p);

  digitalWrite(p, LOW);
  pinMode(p, OUTPUT);
  delayMicroseconds(200);
  int outLow = digitalRead(p);
  uint32_t gpfOut = GPF(p);
  uint32_t gpcOut = GPC(p);
  uint8_t outGpe = (GPE & mask) ? 1 : 0;
  uint8_t outGpo = (GPO & mask) ? 1 : 0;

  // Hold D2 low for a full 1000ms and sample periodically
  int low0 = outLow;
  int low10 = -1, low100 = -1, low500 = -1, low1000 = -1;
  unsigned long holdStart = millis();
  while (millis() - holdStart < 1000) {
    unsigned long elapsed = millis() - holdStart;
    if (low10 < 0 && elapsed >= 10) low10 = digitalRead(p);
    if (low100 < 0 && elapsed >= 100) low100 = digitalRead(p);
    if (low500 < 0 && elapsed >= 500) low500 = digitalRead(p);
    delay(5);
    yield();
  }
  low1000 = digitalRead(p);
  consoleLog((low1000 == 0) ? CLOG_INFO : CLOG_WARN,
             String("[1W-GPIO] D2 1s hold LOW samples (0/10/100/500/1000ms)=") +
             low0 + "/" + low10 + "/" + low100 + "/" + low500 + "/" + low1000);

  digitalWrite(p, HIGH);
  delayMicroseconds(200);
  int outHigh = digitalRead(p);
  uint8_t hiGpo = (GPO & mask) ? 1 : 0;

  // Test INPUT with floating, internal pull-up (GPFPU), and internal pull-down bit (GPFPD)
  pinMode(p, INPUT);
  GPF(p) = GPFFS(GPFFS_GPIO(p));
  delayMicroseconds(100);
  int floatVal = digitalRead(p);
  uint32_t gpfFloat = GPF(p);

  GPF(p) = GPFFS(GPFFS_GPIO(p)) | (1 << GPFPU);
  delayMicroseconds(100);
  int puBitVal = digitalRead(p);
  uint32_t gpfPu = GPF(p);

  GPF(p) = GPFFS(GPFFS_GPIO(p)) | (1 << GPFPD);
  delayMicroseconds(100);
  int pdBitVal = digitalRead(p);
  uint32_t gpfPd = GPF(p);

  GPF(p) = GPFFS(GPFFS_GPIO(p));

  consoleLog(CLOG_INFO,
             String("[1W-GPIO] D2 push-pull L/H=") + outLow + "/" + outHigh +
             " GPO(L/H)=" + outGpo + "/" + hiGpo +
             " float=" + floatVal + "(0x" + String(gpfFloat, HEX) + ")" +
             " PU=" + puBitVal + "(0x" + String(gpfPu, HEX) + ")" +
             " PD=" + pdBitVal + "(0x" + String(gpfPd, HEX) + ")");

  pinMode(p, OUTPUT_OPEN_DRAIN);
  digitalWrite(p, LOW);
  delayMicroseconds(20);
  int odLow = digitalRead(p);

  digitalWrite(p, HIGH);
  delayMicroseconds(20);
  int odHigh = digitalRead(p);

  pinMode(p, INPUT);
  delayMicroseconds(20);
  GPOC = mask;
  GPE |= mask;
  delayMicroseconds(20);
  int regLow = (GPI & mask) ? 1 : 0;
  GPE &= ~mask;
  delayMicroseconds(20);
  int regRel = (GPI & mask) ? 1 : 0;

  consoleLog((outLow == 0 && odLow == 0 && regLow == 0) ? CLOG_INFO : CLOG_WARN,
             String("[1W-GPIO] in=") + inVal + " pu=" + puVal +
             " out(L/H)=" + outLow + "/" + outHigh +
             " od(L/H)=" + odLow + "/" + odHigh +
             " reg(L/rel)=" + regLow + "/" + regRel +
             " outGPF=0x" + String(gpfOut, HEX) + " outGPC=0x" + String(gpcOut, HEX) +
             " GPE=" + outGpe + " GPO=" + outGpo);

  // Compare unconnected pins D1 (GPIO5) and D7 (GPIO13), then restore INPUT
  auto testUnconnectedPin = [](const char* label, uint8_t pin) {
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(20);
    int pu = digitalRead(pin);
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    delayMicroseconds(20);
    int oL = digitalRead(pin);
    digitalWrite(pin, HIGH);
    delayMicroseconds(20);
    int oH = digitalRead(pin);
    pinMode(pin, OUTPUT_OPEN_DRAIN);
    digitalWrite(pin, LOW);
    delayMicroseconds(20);
    int odL = digitalRead(pin);
    pinMode(pin, INPUT);
    consoleLog((oL == 0 && oH == 1 && odL == 0) ? CLOG_INFO : CLOG_WARN,
               String("[1W-GPIO] ") + label + "(GPIO" + pin + ") pu=" + pu +
               " out(L/H)=" + oL + "/" + oH + " odL=" + odL);
  };
  testUnconnectedPin("D1", D1);
  testUnconnectedPin("D7", D7);
}

void initSensorBus() {
  runGpioStartupDiag();
  oneWire.begin(oneWirePin);
  ds.setWaitForConversion(false);
  ds.begin();
  calibrateOneWireBus();
}

String defaultSensorNameForAddress(const DeviceAddress addr) {
  // Use last 5 hex digits of address (low nibble of addr[5], full addr[6], full addr[7])
  uint32_t low20 = ((uint32_t)(addr[5] & 0x0F) << 16) | ((uint32_t)addr[6] << 8) | addr[7];
  char buf[16];
  snprintf(buf, sizeof(buf), "sens%05X", low20);
  return String(buf);
}

static void clearFakeSensorEffects() {
  useFakeSensors = false;
  for (uint8_t i = 0; i < maxsensors; i++) {
    sensorPresent[i] = false;
    sensorTempsC[i] = NAN;
    sensorNames[i][0] = 0;
    memset(sensorAddresses[i], 0, sizeof(DeviceAddress));
  }
  sensorCount = 0;
  displayStartSensor = 0;
}

static void loadFakeSensors() {
  useFakeSensors = true;
  sensorCount = 3;
  for (uint8_t i = 0; i < maxsensors; i++) {
    sensorPresent[i] = false;
    sensorTempsC[i] = NAN;
    sensorNames[i][0] = 0;
    memset(sensorAddresses[i], 0, sizeof(DeviceAddress));
  }
  for (uint8_t i = 0; i < 3; i++) {
    sensorPresent[i] = true;
    sensorTempsC[i] = fakeSensorTempsC[i];
    strlcpy(sensorNames[i], fakeSensorNames[i], sensornamelen);
  }
}

void scanSensors(bool force) {
  if (!config.sensorNetworkEnabled) return;
  if (!force && lastSensorRescanMs > 0 && millis() - lastSensorRescanMs < sensorrescanintervalms) return;
  lastSensorRescanMs = millis();

  flashBlueLed(30);

  DeviceAddress discovered[maxsensors];
  bool duplicateFound = false;
  uint8_t found = 0;

  oneWire.resetReadGlitchCount();

  for (uint8_t attempt = 1; attempt <= 3 && found == 0; attempt++) {
    uint8_t pres = oneWire.reset();
    bool idleHigh = oneWire.getLastResetIdleHigh();
    bool drivenLowOk = oneWire.getLastResetDrivenLowOk();
    uint16_t pStart = oneWire.getLastPresenceStartUs();
    uint16_t pEnd = oneWire.getLastPresenceEndUs();

    oneWire.reset_search();
    DeviceAddress addr;
    uint8_t rawCount = 0;
    uint8_t crcFailCount = 0;

    while (found < maxsensors && oneWire.search(addr)) {
      yield();
      rawCount++;
      uint8_t expectedCrc = OneWire::crc8(addr, 7);
      bool crcOk = (expectedCrc == addr[7]) && ds.validAddress(addr);
      if (!crcOk) {
        crcFailCount++;
        consoleLog(CLOG_WARN, String("[1W-SCAN] pass ") + attempt + " raw=" + formatAddrHex(addr) +
                   " CRC_FAIL(calc=" + String(expectedCrc, HEX) + " got=" + String(addr[7], HEX) + ")");
        continue;
      }
      bool seen = false;
      for (uint8_t i = 0; i < found; i++) {
        if (memcmp(discovered[i], addr, sizeof(DeviceAddress)) == 0) { seen = true; duplicateFound = true; break; }
      }
      if (seen) continue;
      memcpy(discovered[found], addr, sizeof(DeviceAddress));
      found++;
      consoleLog(CLOG_INFO, String("[1W-SCAN] pass ") + attempt + " found=" + formatAddrHex(addr));
    }

    if (found == 0 || crcFailCount > 0) {
      uint8_t failReason = oneWire.getLastSearchFailReason();
      uint8_t failBit = oneWire.getLastSearchFailBit();
      String diag = String("[1W-SCAN] pass ") + attempt +
                    " idle=" + (idleHigh ? "HIGH" : "LOW") +
                    " drivenLow=" + (drivenLowOk ? "OK" : "FAIL") +
                    " reset=" + String(pres) +
                    (pres ? ("(" + String(pStart) + ".." + String(pEnd) + "us)") : "") +
                    " raw=" + String(rawCount) +
                    " valid=" + String(found) +
                    " crcFail=" + String(crcFailCount) +
                    " glitches=" + String(oneWire.getReadGlitchCount()) +
                    " delay=" + String(oneWire.getReadSampleDelayUs()) + "us" +
                    " GPF=0x" + String(GPF(oneWirePin), HEX) +
                    " GPC=0x" + String(GPC(oneWirePin), HEX);
      if (failReason == 1) diag += " fail=RESET_NO_PRESENCE";
      else if (failReason == 2) diag += " fail=NO_DEV_11_AT_BIT_" + String(failBit);
      consoleLog(found > 0 ? CLOG_INFO : CLOG_WARN, diag);
    }

    if (found == 0 && attempt < 3) {
      yield();
    }
  }

  for (uint8_t i = 0; i < maxsensors; i++) {
    sensorPresent[i] = false;
    sensorTempsC[i] = NAN;
    memset(sensorAddresses[i], 0, sizeof(DeviceAddress));
  }

  if (found > 0) {
    if (useFakeSensors && !everHadPhysicalSensors) clearFakeSensorEffects();
    sensorNetworkDetected = true;
    everHadPhysicalSensors = true;
    useFakeSensors = false;
    sensorCount = found;
    for (uint8_t i = 0; i < found; i++) {
      memcpy(sensorAddresses[i], discovered[i], sizeof(DeviceAddress));
      sensorPresent[i] = true;
      ds.setResolution(sensorAddresses[i], 12);
    }
    resolveSensorNamesFromAddresses();
    saveSensorNames();
    if (duplicateFound) setStatusMessage("1-wire dup skipped", 2000);
  } else if (!sensorNetworkDetected && config.simulatedSensorsEnabled) {
    loadFakeSensors();
  } else {
    useFakeSensors = false;
    sensorCount = 0;
    setStatusMessage(sensorNetworkDetected ? "1-wire missing" : "no 1-wire", 2000);
  }
}

void requestTemperatureConversion() {
  if (!config.sensorNetworkEnabled) return;
  if (useFakeSensors || sensorCount == 0) return;
  pulseSpinnerDot(900);
  flashBlueLed(30);
  ds.requestTemperatures();
  conversionPending = true;
  conversionRequestedMs = millis();
}

void collectTemperatureResults() {
  if (!config.sensorNetworkEnabled) return;
  if (useFakeSensors || !conversionPending) return;
  conversionPending = false;
  for (uint8_t i = 0; i < sensorCount; i++) {
    yield();
    float t = ds.getTempC(sensorAddresses[i]);
    if (t == DEVICE_DISCONNECTED_C) {
      sensorTempsC[i] = NAN;
      sensorPresent[i] = false;
    } else {
      sensorTempsC[i] = t;
      sensorPresent[i] = true;
    }
  }
  lastSensorSampleMs = millis();
}

void readTemperatures() {
  if (!config.sensorNetworkEnabled) return;
  if (useFakeSensors) return;
  pulseSpinnerDot(900);
  flashBlueLed(30);
  ds.requestTemperatures();
  unsigned long start = millis();
  while (millis() - start < 800) { yield(); }
  for (uint8_t i = 0; i < sensorCount; i++) {
    yield();
    float t = ds.getTempC(sensorAddresses[i]);
    if (t == DEVICE_DISCONNECTED_C) {
      sensorTempsC[i] = NAN;
      sensorPresent[i] = false;
    } else {
      sensorTempsC[i] = t;
      sensorPresent[i] = true;
    }
  }
}

void sampleSensors() {
  if (!config.sensorNetworkEnabled) return;
  scanSensors();
  requestTemperatureConversion();
}

String sensorAddressString(uint8_t i) {
  if (useFakeSensors && i < 3) return String(fakeSensorAddresses[i]);
  return addressToString(sensorAddresses[i]);
}
#endif // SHARED_LIB_USE_ONEWIRE
