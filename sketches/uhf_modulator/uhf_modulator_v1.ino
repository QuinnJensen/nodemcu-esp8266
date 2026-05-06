/*
  uhf_modulator.ino
  First draft ESP8266 NodeMCU 433 MHz UHF/OOK transmitter with WiFiManager,
  MQTT control, LittleFS-persistent timing profiles and code table, OLED status,
  and deterministic busy-wait pulse modulation.

  Based conceptually on temp_network_v4_water_probe.ino.txt and the uploaded
  giandel.pl / heater.pl / rpi_rf.py timing model.

  Board assumptions:
    - NodeMCU / ESP8266
    - D3 / GPIO0 = RF data output (requested by user)
    - D4 / GPIO2 = onboard blue LED, active LOW; mirrored during TX
    - D5 / GPIO14 = I2C SDA for SSD1306 OLED
    - D6 / GPIO12 = I2C SCL for SSD1306 OLED

  Notes:
    - D3/GPIO0 is a boot strap pin. Keep the transmitter DATA input from pulling
      it LOW at boot. Most common ASK transmitter DATA pins are high impedance,
      but verify on your hardware.
    - TX timing in this first draft uses interrupts-disabled busy waits for each
      pulse pair. That is much more deterministic than Linux userspace GPIO, but
      still a first draft. Further refinement could move the pulse scheduler into
      timer1 ISR if needed.
    - The onboard LED is mirrored during TX only; because it is active LOW while
      RF data is active HIGH, the firmware explicitly drives it LOW when the RF
      output is HIGH and HIGH when the RF output is LOW.

  Persistent files in LittleFS:
    - /config.json     WiFi/MQTT/general config
    - /profiles.json   timing profile table
    - /codes.json      code table

  MQTT topic layout:
    <baseTopic>/<deviceId>/command
    <baseTopic>/<deviceId>/status
    <baseTopic>/<deviceId>/results
    <baseTopic>/<deviceId>/tx

  Seeded timing/code sets:
    - proto1_350us         protocol-1 style default
    - proto1_354us_compat  compatibility profile for prior Pi workaround
    - giandel_on/off
    - heater_on/off/plus/minus

  Example command payloads:
    {"command":"tx","id":"giandel_on"}
    {"command":"txraw","profileId":"proto1_350us","value":14199672,"bits":24,"repeat":10}
    {"command":"upsertprofile","profile":{...}}
    {"command":"upsertcode","code":{...}}
    {"command":"replaceprofiles","profiles":[...]}
    {"command":"replacecodes","codes":[...]}
    {"command":"listprofiles"}
    {"command":"listcodes"}
    {"command":"getprofile","id":"proto1_350us"}
    {"command":"getcode","id":"giandel_on"}
    {"command":"deleteprofile","id":"proto1_354us_compat"}
    {"command":"deletecode","id":"heater_minus"}
*/

#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <ctype.h>

using namespace ArduinoJson;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

#define I2C_SDA D5
#define I2C_SCL D6
#define FORCE_PORTAL_PIN D3   // user requested D3 as TX output, so portal button must move if needed
#define TX_PIN D3             // GPIO0 user requested
#define LED_PIN D4            // onboard LED active LOW

#define CONFIG_FILE "/config.json"
#define PROFILES_FILE "/profiles.json"
#define CODES_FILE "/codes.json"

#define DISPLAY_INTERVAL_MS 250UL
#define STATUS_HEARTBEAT_INTERVAL_MS 60000UL
#define MQTT_RETRY_MS 3000UL
#define PORTAL_TIMEOUT_SEC 180
#define STARTUP_RECONFIG_COUNTDOWN_MS 10000UL
#define MQTT_BUFFER_SIZE 4096
#define DEVICE_TZ "MST7MDT,M3.2.0,M11.1.0"

#define MAX_PROFILES 16
#define MAX_CODES 32
#define ID_LEN 32
#define MESSAGE_LEN 96

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
ESP8266WebServer* metricsServer = nullptr;
bool metricsServerStarted = false;

struct TimingProfile {
  char id[ID_LEN];
  uint16_t pulseUs;
  uint16_t syncHigh;
  uint16_t syncLow;
  uint16_t zeroHigh;
  uint16_t zeroLow;
  uint16_t oneHigh;
  uint16_t oneLow;
  uint16_t defaultBits;
  uint16_t defaultRepeat;
  bool active;
};

struct CodeRecord {
  char id[ID_LEN];
  char profileId[ID_LEN];
  uint32_t value;
  uint16_t bits;
  uint16_t repeat;
  bool active;
};

struct AppConfig {
  char mqttHost[64] = "192.168.1.50";
  char baseTopic[64] = "uhfmod";
  char deviceId[32] = "uhf_modulator";
  uint16_t mqttPort = 1883;
  uint16_t prometheusPort = 9111;
  uint16_t txIdleLevel = LOW;
} config;

TimingProfile profiles[MAX_PROFILES];
CodeRecord codes[MAX_CODES];
uint8_t profileCount = 0;
uint8_t codeCount = 0;

char commandTopic[128];
char statusTopic[128];
char resultsTopic[128];
char txTopic[128];

bool shouldSaveConfig = false;
bool portalActive = false;
bool mqttOnlinePublished = false;
unsigned long lastDisplayMs = 0;
unsigned long lastStatusHeartbeatMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastTrafficAnimMs = 0;
unsigned long statusMsgUntilMs = 0;
unsigned long bootMillis = 0;
unsigned long mqttPublishCount = 0;
unsigned long metricsScrapeCount = 0;
unsigned long txCount = 0;
int lastRssi = -127;
bool mqttTrafficActive = false;
uint8_t spinnerFrame = 0;
String lastRxType = "-";
String lastStatusMsg = "booting";
String lastRxRaw;
bool txBusy = false;
char lastTxId[ID_LEN] = "-";
char lastTxProfileId[ID_LEN] = "-";
uint32_t lastTxValue = 0;
uint16_t lastTxBits = 0;
uint16_t lastTxRepeat = 0;
unsigned long lastTxMs = 0;

void buildTopics();
bool loadConfig();
bool saveConfig();
void saveConfigCallback();
void startPortalAndConnect(bool forcePortal);
bool startupReconfigRequested();
void showStartupReconfigCountdown(uint8_t secondsLeft);
void ensureWiFi();
bool mqttConnect();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleCommandJson(const String& payload);
void publishStatus(bool retained = false);
void publishCommandResult(const char* type, bool ok, const char* message);
bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained = false);
String sanitizeTopicPart(const String& in);
String safeDeviceId();
String ipToString(IPAddress ip);
void setupTime();
String timeToString(time_t t);
String currentTimestampString();
void setStatusMessage(const String& msg, unsigned long holdMs = 3000);
void kickActivitySpinner(unsigned long durationMs = 1500);
void renderDisplay();
void drawSpinner(int cx, int cy, uint8_t frame);
void startMetricsServer();
String buildPrometheusMetrics();
String prometheusEscaped(const String& in);

void clearProfiles();
void clearCodes();
void seedDefaultProfiles();
void seedDefaultCodes();
bool loadProfiles();
bool saveProfiles();
bool loadCodes();
bool saveCodes();
int findProfileIndexById(const char* id);
int findCodeIndexById(const char* id);
bool validateId(const char* s);
bool profileFromJson(JsonObjectConst obj, TimingProfile& out);
bool codeFromJson(JsonObjectConst obj, CodeRecord& out);
void appendProfilesToJson(JsonDocument& doc);
void appendCodesToJson(JsonDocument& doc);
bool upsertProfile(const TimingProfile& p, bool persist = true);
bool upsertCode(const CodeRecord& c, bool persist = true);
bool deleteProfileById(const char* id);
bool deleteCodeById(const char* id);
bool profileIsInUse(const char* profileId);

void setRfAndLed(bool high);
void txDelayUs(uint32_t us);
bool transmitWaveform(const TimingProfile& p, uint32_t value, uint16_t bits, uint16_t repeat);
bool transmitCodeRecord(const CodeRecord& c);

String sanitizeTopicPart(const String& in) {
  String out;
  out.reserve(in.length());
  for (uint16_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (isalnum(static_cast<unsigned char>(c))) out += char(tolower(static_cast<unsigned char>(c)));
    else if (c == '-' || c == '_' || c == '.') out += c;
    else if (c == ' ') out += '_';
  }
  if (!out.length()) out = "node";
  return out;
}

String safeDeviceId() {
  String id(config.deviceId);
  id.trim();
  if (!id.length()) id = "uhf_modulator";
  return id;
}

void buildTopics() {
  String idPart = sanitizeTopicPart(safeDeviceId());
  snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", config.baseTopic, idPart.c_str());
  snprintf(statusTopic, sizeof(statusTopic), "%s/%s/status", config.baseTopic, idPart.c_str());
  snprintf(resultsTopic, sizeof(resultsTopic), "%s/%s/results", config.baseTopic, idPart.c_str());
  snprintf(txTopic, sizeof(txTopic), "%s/%s/tx", config.baseTopic, idPart.c_str());
}

void clearProfiles() {
  profileCount = 0;
  for (uint8_t i = 0; i < MAX_PROFILES; i++) profiles[i].active = false;
}

void clearCodes() {
  codeCount = 0;
  for (uint8_t i = 0; i < MAX_CODES; i++) codes[i].active = false;
}

bool validateId(const char* s) {
  if (!s || !s[0]) return false;
  size_t n = strlen(s);
  if (n >= ID_LEN) return false;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
  }
  return true;
}

int findProfileIndexById(const char* id) {
  for (uint8_t i = 0; i < profileCount; i++) {
    if (profiles[i].active && !strcmp(profiles[i].id, id)) return i;
  }
  return -1;
}

int findCodeIndexById(const char* id) {
  for (uint8_t i = 0; i < codeCount; i++) {
    if (codes[i].active && !strcmp(codes[i].id, id)) return i;
  }
  return -1;
}

void seedDefaultProfiles() {
  clearProfiles();
  TimingProfile p;

  memset(&p, 0, sizeof(p));
  strlcpy(p.id, "proto1_350us", sizeof(p.id));
  p.pulseUs = 350; p.syncHigh = 1; p.syncLow = 31; p.zeroHigh = 1; p.zeroLow = 3; p.oneHigh = 3; p.oneLow = 1; p.defaultBits = 24; p.defaultRepeat = 10; p.active = true;
  upsertProfile(p, false);

  memset(&p, 0, sizeof(p));
  strlcpy(p.id, "proto1_354us_compat", sizeof(p.id));
  p.pulseUs = 354; p.syncHigh = 1; p.syncLow = 31; p.zeroHigh = 1; p.zeroLow = 3; p.oneHigh = 3; p.oneLow = 1; p.defaultBits = 24; p.defaultRepeat = 25; p.active = true;
  upsertProfile(p, false);

  saveProfiles();
}

void seedDefaultCodes() {
  clearCodes();
  CodeRecord c;

  memset(&c, 0, sizeof(c));
  strlcpy(c.id, "giandel_on", sizeof(c.id));
  strlcpy(c.profileId, "proto1_350us", sizeof(c.profileId));
  c.value = 14199672UL; c.bits = 24; c.repeat = 10; c.active = true; upsertCode(c, false);

  memset(&c, 0, sizeof(c));
  strlcpy(c.id, "giandel_off", sizeof(c.id));
  strlcpy(c.profileId, "proto1_350us", sizeof(c.profileId));
  c.value = 14199668UL; c.bits = 24; c.repeat = 10; c.active = true; upsertCode(c, false);

  memset(&c, 0, sizeof(c));
  strlcpy(c.id, "heater_on", sizeof(c.id));
  strlcpy(c.profileId, "proto1_350us", sizeof(c.profileId));
  c.value = 757795032UL; c.bits = 31; c.repeat = 1; c.active = true; upsertCode(c, false);

  memset(&c, 0, sizeof(c));
  strlcpy(c.id, "heater_off", sizeof(c.id));
  strlcpy(c.profileId, "proto1_350us", sizeof(c.profileId));
  c.value = 757793412UL; c.bits = 31; c.repeat = 1; c.active = true; upsertCode(c, false);

  memset(&c, 0, sizeof(c));
  strlcpy(c.id, "heater_plus", sizeof(c.id));
  strlcpy(c.profileId, "proto1_350us", sizeof(c.profileId));
  c.value = 757793912UL; c.bits = 31; c.repeat = 1; c.active = true; upsertCode(c, false);

  memset(&c, 0, sizeof(c));
  strlcpy(c.id, "heater_minus", sizeof(c.id));
  strlcpy(c.profileId, "proto1_350us", sizeof(c.profileId));
  c.value = 757793092UL; c.bits = 31; c.repeat = 1; c.active = true; upsertCode(c, false);

  saveCodes();
}

bool profileFromJson(JsonObjectConst obj, TimingProfile& out) {
  const char* id = obj["id"] | "";
  if (!validateId(id)) return false;
  uint32_t pulseUs = obj["pulse_us"] | 0;
  uint32_t syncHigh = obj["sync_high"] | 0;
  uint32_t syncLow = obj["sync_low"] | 0;
  uint32_t zeroHigh = obj["zero_high"] | 0;
  uint32_t zeroLow = obj["zero_low"] | 0;
  uint32_t oneHigh = obj["one_high"] | 0;
  uint32_t oneLow = obj["one_low"] | 0;
  uint32_t defaultBits = obj["default_bits"] | 24;
  uint32_t defaultRepeat = obj["default_repeat"] | 10;
  if (pulseUs < 50 || pulseUs > 5000) return false;
  if (!syncHigh || !syncLow || !zeroHigh || !zeroLow || !oneHigh || !oneLow) return false;
  if (defaultBits < 1 || defaultBits > 64) return false;
  if (defaultRepeat < 1 || defaultRepeat > 100) return false;
  memset(&out, 0, sizeof(out));
  strlcpy(out.id, id, sizeof(out.id));
  out.pulseUs = pulseUs;
  out.syncHigh = syncHigh;
  out.syncLow = syncLow;
  out.zeroHigh = zeroHigh;
  out.zeroLow = zeroLow;
  out.oneHigh = oneHigh;
  out.oneLow = oneLow;
  out.defaultBits = defaultBits;
  out.defaultRepeat = defaultRepeat;
  out.active = true;
  return true;
}

bool codeFromJson(JsonObjectConst obj, CodeRecord& out) {
  const char* id = obj["id"] | "";
  const char* profileId = obj["profileId"] | "";
  if (!validateId(id) || !validateId(profileId)) return false;
  if (findProfileIndexById(profileId) < 0) return false;
  uint32_t value = obj["value"] | 0;
  uint32_t bits = obj["bits"] | 0;
  uint32_t repeat = obj["repeat"] | 0;
  if (bits < 1 || bits > 64) return false;
  if (repeat > 100) return false;
  memset(&out, 0, sizeof(out));
  strlcpy(out.id, id, sizeof(out.id));
  strlcpy(out.profileId, profileId, sizeof(out.profileId));
  out.value = value;
  out.bits = bits;
  out.repeat = repeat;
  out.active = true;
  return true;
}

bool upsertProfile(const TimingProfile& p, bool persist) {
  int idx = findProfileIndexById(p.id);
  if (idx >= 0) {
    profiles[idx] = p;
  } else {
    if (profileCount >= MAX_PROFILES) return false;
    profiles[profileCount++] = p;
  }
  return persist ? saveProfiles() : true;
}

bool upsertCode(const CodeRecord& c, bool persist) {
  int idx = findCodeIndexById(c.id);
  if (idx >= 0) {
    codes[idx] = c;
  } else {
    if (codeCount >= MAX_CODES) return false;
    codes[codeCount++] = c;
  }
  return persist ? saveCodes() : true;
}

bool profileIsInUse(const char* profileId) {
  for (uint8_t i = 0; i < codeCount; i++) {
    if (codes[i].active && !strcmp(codes[i].profileId, profileId)) return true;
  }
  return false;
}

bool deleteProfileById(const char* id) {
  int idx = findProfileIndexById(id);
  if (idx < 0) return false;
  if (profileIsInUse(id)) return false;
  for (uint8_t i = idx; i + 1 < profileCount; i++) profiles[i] = profiles[i + 1];
  if (profileCount) profileCount--;
  return saveProfiles();
}

bool deleteCodeById(const char* id) {
  int idx = findCodeIndexById(id);
  if (idx < 0) return false;
  for (uint8_t i = idx; i + 1 < codeCount; i++) codes[i] = codes[i + 1];
  if (codeCount) codeCount--;
  return saveCodes();
}

bool loadProfiles() {
  clearProfiles();
  if (!LittleFS.exists(PROFILES_FILE)) {
    seedDefaultProfiles();
    return true;
  }
  File f = LittleFS.open(PROFILES_FILE, "r");
  if (!f) return false;
  StaticJsonDocument<3072> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  JsonArrayConst arr = doc["profiles"].as<JsonArrayConst>();
  if (arr.isNull()) return false;
  for (JsonObjectConst obj : arr) {
    if (profileCount >= MAX_PROFILES) break;
    TimingProfile p;
    if (profileFromJson(obj, p)) profiles[profileCount++] = p;
  }
  if (!profileCount) seedDefaultProfiles();
  return true;
}

bool saveProfiles() {
  StaticJsonDocument<3072> doc;
  JsonArray arr = doc.createNestedArray("profiles");
  for (uint8_t i = 0; i < profileCount; i++) {
    JsonObject p = arr.createNestedObject();
    p["id"] = profiles[i].id;
    p["pulse_us"] = profiles[i].pulseUs;
    p["sync_high"] = profiles[i].syncHigh;
    p["sync_low"] = profiles[i].syncLow;
    p["zero_high"] = profiles[i].zeroHigh;
    p["zero_low"] = profiles[i].zeroLow;
    p["one_high"] = profiles[i].oneHigh;
    p["one_low"] = profiles[i].oneLow;
    p["default_bits"] = profiles[i].defaultBits;
    p["default_repeat"] = profiles[i].defaultRepeat;
  }
  File f = LittleFS.open(PROFILES_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool loadCodes() {
  clearCodes();
  if (!LittleFS.exists(CODES_FILE)) {
    seedDefaultCodes();
    return true;
  }
  File f = LittleFS.open(CODES_FILE, "r");
  if (!f) return false;
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  JsonArrayConst arr = doc["codes"].as<JsonArrayConst>();
  if (arr.isNull()) return false;
  for (JsonObjectConst obj : arr) {
    if (codeCount >= MAX_CODES) break;
    CodeRecord c;
    if (codeFromJson(obj, c)) codes[codeCount++] = c;
  }
  if (!codeCount) seedDefaultCodes();
  return true;
}

bool saveCodes() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.createNestedArray("codes");
  for (uint8_t i = 0; i < codeCount; i++) {
    JsonObject c = arr.createNestedObject();
    c["id"] = codes[i].id;
    c["profileId"] = codes[i].profileId;
    c["value"] = codes[i].value;
    c["bits"] = codes[i].bits;
    c["repeat"] = codes[i].repeat;
  }
  File f = LittleFS.open(CODES_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

bool loadConfig() {
  if (!LittleFS.begin()) return false;
  if (!LittleFS.exists(CONFIG_FILE)) {
    buildTopics();
    return false;
  }
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) {
    buildTopics();
    return false;
  }
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    buildTopics();
    return false;
  }
  strlcpy(config.mqttHost, doc["mqtthost"] | "192.168.1.50", sizeof(config.mqttHost));
  strlcpy(config.baseTopic, doc["basetopic"] | "uhfmod", sizeof(config.baseTopic));
  strlcpy(config.deviceId, doc["deviceid"] | "uhf_modulator", sizeof(config.deviceId));
  config.mqttPort = doc["mqttport"] | 1883;
  config.prometheusPort = doc["prometheusport"] | 9111;
  config.txIdleLevel = doc["txidlelevel"] | LOW;
  buildTopics();
  return true;
}

bool saveConfig() {
  StaticJsonDocument<512> doc;
  doc["mqtthost"] = config.mqttHost;
  doc["mqttport"] = config.mqttPort;
  doc["basetopic"] = config.baseTopic;
  doc["deviceid"] = safeDeviceId();
  doc["prometheusport"] = config.prometheusPort;
  doc["txidlelevel"] = config.txIdleLevel;
  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

void saveConfigCallback() { shouldSaveConfig = true; }

String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

void setupTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", DEVICE_TZ, 1);
  tzset();
}

String timeToString(time_t t) {
  if (t < 100000) return "";
  struct tm tmStruct;
  localtime_r(&t, &tmStruct);
  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tmStruct);
  String ts(buf);
  if (ts.length() == 24) ts = ts.substring(0, 22) + ":" + ts.substring(22);
  return ts;
}

String currentTimestampString() {
  time_t now = time(nullptr);
  return timeToString(now);
}

void setStatusMessage(const String& msg, unsigned long holdMs) {
  lastStatusMsg = msg;
  statusMsgUntilMs = millis() + holdMs;
  Serial.println(msg);
}

void kickActivitySpinner(unsigned long durationMs) {
  mqttTrafficActive = true;
  lastTrafficAnimMs = millis() + durationMs - 1500UL;
  spinnerFrame = (spinnerFrame + 1) & 0x07;
}

bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained) {
  if (!mqtt.connected()) return false;
  char buffer[MQTT_BUFFER_SIZE];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));
  bool ok = mqtt.publish(topic, (const uint8_t*)buffer, n, retained);
  if (ok) {
    mqttPublishCount++;
    kickActivitySpinner();
  }
  return ok;
}

void publishCommandResult(const char* type, bool ok, const char* message) {
  StaticJsonDocument<384> reply;
  reply["type"] = type;
  reply["id"] = safeDeviceId();
  reply["ok"] = ok;
  reply["message"] = message;
  publishJsonDocToTopic(resultsTopic, reply, false);
}

void appendProfilesToJson(JsonDocument& doc) {
  JsonArray arr = doc.createNestedArray("profiles");
  for (uint8_t i = 0; i < profileCount; i++) {
    JsonObject p = arr.createNestedObject();
    p["id"] = profiles[i].id;
    p["pulse_us"] = profiles[i].pulseUs;
    p["sync_high"] = profiles[i].syncHigh;
    p["sync_low"] = profiles[i].syncLow;
    p["zero_high"] = profiles[i].zeroHigh;
    p["zero_low"] = profiles[i].zeroLow;
    p["one_high"] = profiles[i].oneHigh;
    p["one_low"] = profiles[i].oneLow;
    p["default_bits"] = profiles[i].defaultBits;
    p["default_repeat"] = profiles[i].defaultRepeat;
  }
}

void appendCodesToJson(JsonDocument& doc) {
  JsonArray arr = doc.createNestedArray("codes");
  for (uint8_t i = 0; i < codeCount; i++) {
    JsonObject c = arr.createNestedObject();
    c["id"] = codes[i].id;
    c["profileId"] = codes[i].profileId;
    c["value"] = codes[i].value;
    c["bits"] = codes[i].bits;
    c["repeat"] = codes[i].repeat;
  }
}

void setRfAndLed(bool high) {
  digitalWrite(TX_PIN, high ? HIGH : LOW);
  digitalWrite(LED_PIN, high ? LOW : HIGH);
}

void txDelayUs(uint32_t us) {
  uint32_t start = micros();
  while ((uint32_t)(micros() - start) < us) {
    ESP.wdtFeed();
  }
}

bool transmitWaveform(const TimingProfile& p, uint32_t value, uint16_t bits, uint16_t repeat) {
  if (bits < 1 || bits > 32) return false;
  txBusy = true;
  strlcpy(lastTxProfileId, p.id, sizeof(lastTxProfileId));
  lastTxValue = value;
  lastTxBits = bits;
  lastTxRepeat = repeat;
  noInterrupts();
  for (uint16_t r = 0; r < repeat; r++) {
    for (int8_t b = bits - 1; b >= 0; b--) {
      bool one = ((value >> b) & 0x1U) != 0;
      uint32_t highUs = (one ? p.oneHigh : p.zeroHigh) * (uint32_t)p.pulseUs;
      uint32_t lowUs = (one ? p.oneLow : p.zeroLow) * (uint32_t)p.pulseUs;
      setRfAndLed(true);
      txDelayUs(highUs);
      setRfAndLed(false);
      txDelayUs(lowUs);
    }
    setRfAndLed(true);
    txDelayUs((uint32_t)p.syncHigh * p.pulseUs);
    setRfAndLed(false);
    txDelayUs((uint32_t)p.syncLow * p.pulseUs);
  }
  interrupts();
  digitalWrite(TX_PIN, config.txIdleLevel ? HIGH : LOW);
  digitalWrite(LED_PIN, HIGH);
  txBusy = false;
  txCount++;
  lastTxMs = millis();
  return true;
}

bool transmitCodeRecord(const CodeRecord& c) {
  int pidx = findProfileIndexById(c.profileId);
  if (pidx < 0) return false;
  strlcpy(lastTxId, c.id, sizeof(lastTxId));
  uint16_t bits = c.bits ? c.bits : profiles[pidx].defaultBits;
  uint16_t repeat = c.repeat ? c.repeat : profiles[pidx].defaultRepeat;
  return transmitWaveform(profiles[pidx], c.value, bits, repeat);
}

void publishStatus(bool retained) {
  if (!mqtt.connected()) return;
  StaticJsonDocument<2048> doc;
  doc["type"] = "status";
  doc["id"] = safeDeviceId();
  doc["online"] = true;
  doc["ssid"] = WiFi.SSID();
  doc["rssidbm"] = WiFi.RSSI();
  doc["ip"] = ipToString(WiFi.localIP());
  doc["freeheap"] = ESP.getFreeHeap();
  doc["prometheusport"] = config.prometheusPort;
  doc["profilecount"] = profileCount;
  doc["codecount"] = codeCount;
  doc["txbusy"] = txBusy;
  doc["txcount"] = txCount;
  doc["lasttxid"] = lastTxId;
  doc["lasttxprofileid"] = lastTxProfileId;
  doc["lasttxvalue"] = lastTxValue;
  doc["lasttxbits"] = lastTxBits;
  doc["lasttxrepeat"] = lastTxRepeat;
  if (lastTxMs) doc["lasttxagems"] = millis() - lastTxMs;
  doc["txtopic"] = txTopic;
  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;
  publishJsonDocToTopic(statusTopic, doc, retained);
}

void handleCommandJson(const String& payload) {
  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    lastRxType = "badjson";
    publishCommandResult("error", false, "bad json");
    setStatusMessage("bad json cmd", 2500);
    return;
  }

  const char* command = doc["command"] | doc["type"] | "";
  lastRxType = String(command);

  if (!strcmp(command, "status") || !strcmp(command, "heartbeat")) {
    publishStatus(false);
    setStatusMessage("publishing status", 1500);
    return;
  }

  if (!strcmp(command, "listprofiles")) {
    StaticJsonDocument<3072> reply;
    reply["type"] = "listprofilesreply";
    reply["id"] = safeDeviceId();
    reply["ok"] = true;
    appendProfilesToJson(reply);
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }

  if (!strcmp(command, "listcodes")) {
    StaticJsonDocument<4096> reply;
    reply["type"] = "listcodesreply";
    reply["id"] = safeDeviceId();
    reply["ok"] = true;
    appendCodesToJson(reply);
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }

  if (!strcmp(command, "getprofile")) {
    const char* id = doc["id"] | "";
    int idx = findProfileIndexById(id);
    if (idx < 0) {
      publishCommandResult("getprofilereply", false, "profile not found");
      return;
    }
    StaticJsonDocument<512> reply;
    reply["type"] = "getprofilereply";
    reply["id"] = safeDeviceId();
    reply["ok"] = true;
    JsonObject p = reply.createNestedObject("profile");
    p["id"] = profiles[idx].id;
    p["pulse_us"] = profiles[idx].pulseUs;
    p["sync_high"] = profiles[idx].syncHigh;
    p["sync_low"] = profiles[idx].syncLow;
    p["zero_high"] = profiles[idx].zeroHigh;
    p["zero_low"] = profiles[idx].zeroLow;
    p["one_high"] = profiles[idx].oneHigh;
    p["one_low"] = profiles[idx].oneLow;
    p["default_bits"] = profiles[idx].defaultBits;
    p["default_repeat"] = profiles[idx].defaultRepeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }

  if (!strcmp(command, "getcode")) {
    const char* id = doc["id"] | "";
    int idx = findCodeIndexById(id);
    if (idx < 0) {
      publishCommandResult("getcodereply", false, "code not found");
      return;
    }
    StaticJsonDocument<512> reply;
    reply["type"] = "getcodereply";
    reply["id"] = safeDeviceId();
    reply["ok"] = true;
    JsonObject c = reply.createNestedObject("code");
    c["id"] = codes[idx].id;
    c["profileId"] = codes[idx].profileId;
    c["value"] = codes[idx].value;
    c["bits"] = codes[idx].bits;
    c["repeat"] = codes[idx].repeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }

  if (!strcmp(command, "upsertprofile")) {
    JsonObjectConst pObj = doc["profile"].as<JsonObjectConst>();
    TimingProfile p;
    if (!pObj.isNull() && profileFromJson(pObj, p) && upsertProfile(p, true)) {
      publishCommandResult("upsertprofilereply", true, "profile saved");
    } else {
      publishCommandResult("upsertprofilereply", false, "invalid profile or save failed");
    }
    return;
  }

  if (!strcmp(command, "upsertcode")) {
    JsonObjectConst cObj = doc["code"].as<JsonObjectConst>();
    CodeRecord c;
    if (!cObj.isNull() && codeFromJson(cObj, c) && upsertCode(c, true)) {
      publishCommandResult("upsertcodereply", true, "code saved");
    } else {
      publishCommandResult("upsertcodereply", false, "invalid code or save failed");
    }
    return;
  }

  if (!strcmp(command, "replaceprofiles")) {
    JsonArrayConst arr = doc["profiles"].as<JsonArrayConst>();
    if (arr.isNull() || arr.size() > MAX_PROFILES) {
      publishCommandResult("replaceprofilesreply", false, "invalid profiles array");
      return;
    }
    TimingProfile temp[MAX_PROFILES];
    uint8_t count = 0;
    for (JsonObjectConst obj : arr) {
      TimingProfile p;
      if (!profileFromJson(obj, p)) {
        publishCommandResult("replaceprofilesreply", false, "invalid profile entry");
        return;
      }
      temp[count++] = p;
    }
    for (uint8_t i = 0; i < codeCount; i++) {
      bool found = false;
      for (uint8_t j = 0; j < count; j++) if (!strcmp(codes[i].profileId, temp[j].id)) found = true;
      if (!found) {
        publishCommandResult("replaceprofilesreply", false, "existing code would reference missing profile");
        return;
      }
    }
    clearProfiles();
    for (uint8_t i = 0; i < count; i++) profiles[profileCount++] = temp[i];
    if (saveProfiles()) publishCommandResult("replaceprofilesreply", true, "profiles replaced");
    else publishCommandResult("replaceprofilesreply", false, "save failed");
    return;
  }

  if (!strcmp(command, "replacecodes")) {
    JsonArrayConst arr = doc["codes"].as<JsonArrayConst>();
    if (arr.isNull() || arr.size() > MAX_CODES) {
      publishCommandResult("replacecodesreply", false, "invalid codes array");
      return;
    }
    CodeRecord temp[MAX_CODES];
    uint8_t count = 0;
    for (JsonObjectConst obj : arr) {
      CodeRecord c;
      if (!codeFromJson(obj, c)) {
        publishCommandResult("replacecodesreply", false, "invalid code entry");
        return;
      }
      temp[count++] = c;
    }
    clearCodes();
    for (uint8_t i = 0; i < count; i++) codes[codeCount++] = temp[i];
    if (saveCodes()) publishCommandResult("replacecodesreply", true, "codes replaced");
    else publishCommandResult("replacecodesreply", false, "save failed");
    return;
  }

  if (!strcmp(command, "deleteprofile")) {
    const char* id = doc["id"] | "";
    if (deleteProfileById(id)) publishCommandResult("deleteprofilereply", true, "profile deleted");
    else publishCommandResult("deleteprofilereply", false, "delete failed; not found or in use");
    return;
  }

  if (!strcmp(command, "deletecode")) {
    const char* id = doc["id"] | "";
    if (deleteCodeById(id)) publishCommandResult("deletecodereply", true, "code deleted");
    else publishCommandResult("deletecodereply", false, "delete failed");
    return;
  }

  if (!strcmp(command, "tx")) {
    const char* id = doc["id"] | "";
    int idx = findCodeIndexById(id);
    if (idx < 0) {
      publishCommandResult("txreply", false, "code not found");
      return;
    }
    bool ok = transmitCodeRecord(codes[idx]);
    StaticJsonDocument<512> reply;
    reply["type"] = "txreply";
    reply["id"] = safeDeviceId();
    reply["ok"] = ok;
    reply["codeId"] = codes[idx].id;
    reply["profileId"] = codes[idx].profileId;
    reply["value"] = codes[idx].value;
    reply["bits"] = codes[idx].bits;
    reply["repeat"] = codes[idx].repeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    publishStatus(false);
    return;
  }

  if (!strcmp(command, "txraw")) {
    const char* profileId = doc["profileId"] | "";
    int pidx = findProfileIndexById(profileId);
    if (pidx < 0) {
      publishCommandResult("txrawreply", false, "profile not found");
      return;
    }
    uint32_t value = doc["value"] | 0;
    uint16_t bits = doc["bits"] | profiles[pidx].defaultBits;
    uint16_t repeat = doc["repeat"] | profiles[pidx].defaultRepeat;
    bool ok = transmitWaveform(profiles[pidx], value, bits, repeat);
    StaticJsonDocument<512> reply;
    reply["type"] = "txrawreply";
    reply["id"] = safeDeviceId();
    reply["ok"] = ok;
    reply["profileId"] = profiles[pidx].id;
    reply["value"] = value;
    reply["bits"] = bits;
    reply["repeat"] = repeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    publishStatus(false);
    return;
  }

  if (!strcmp(command, "ls")) {
    StaticJsonDocument<768> reply;
    reply["type"] = "lsreply";
    reply["id"] = safeDeviceId();
    reply["ok"] = true;
    reply["configfile"] = CONFIG_FILE;
    reply["profilesfile"] = PROFILES_FILE;
    reply["codesfile"] = CODES_FILE;
    reply["commandtopic"] = commandTopic;
    reply["statustopic"] = statusTopic;
    reply["resultstopic"] = resultsTopic;
    reply["txtopic"] = txTopic;
    reply["profilecount"] = profileCount;
    reply["codecount"] = codeCount;
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }

  publishCommandResult("error", false, "unknown command");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += char(payload[i]);
  lastRxRaw = msg;
  if (String(topic) == commandTopic) handleCommandJson(msg);
  else lastRxType = "other";
  kickActivitySpinner();
}

bool mqttConnect() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  String clientId = "uhf-modulator-" + sanitizeTopicPart(safeDeviceId()) + "-" + String(ESP.getChipId(), HEX);
  StaticJsonDocument<192> willDoc;
  willDoc["type"] = "status";
  willDoc["id"] = safeDeviceId();
  willDoc["online"] = false;
  char willPayload[192];
  serializeJson(willDoc, willPayload, sizeof(willPayload));
  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr, statusTopic, 0, true, willPayload);
  if (ok) {
    mqtt.subscribe(commandTopic);
    mqttOnlinePublished = false;
    setStatusMessage("broker connected", 2000);
    return true;
  }
  setStatusMessage("broker conn fail", 2000);
  return false;
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    lastRssi = WiFi.RSSI();
    return;
  }
  setStatusMessage("connecting wifi", 2000);
  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    lastRssi = WiFi.RSSI();
    setStatusMessage("ip " + ipToString(WiFi.localIP()), 3000);
  }
}

void showStartupReconfigCountdown(uint8_t secondsLeft) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Starting up...");
  display.setCursor(0, 16);
  display.print("press FLASH to");
  display.setCursor(0, 28);
  display.print("reconfig portal");
  display.setCursor(0, 46);
  display.print("in ");
  display.print(secondsLeft);
  display.print(" sec");
  display.display();
}

bool startupReconfigRequested() {
  pinMode(FORCE_PORTAL_PIN, INPUT_PULLUP);
  unsigned long start = millis();
  uint8_t lastShown = 255;
  while (millis() - start < STARTUP_RECONFIG_COUNTDOWN_MS) {
    unsigned long elapsed = millis() - start;
    uint8_t secondsLeft = uint8_t((STARTUP_RECONFIG_COUNTDOWN_MS - elapsed + 999UL) / 1000UL);
    if (secondsLeft != lastShown) {
      showStartupReconfigCountdown(secondsLeft);
      lastShown = secondsLeft;
    }
    if (digitalRead(FORCE_PORTAL_PIN) == LOW) {
      delay(30);
      if (digitalRead(FORCE_PORTAL_PIN) == LOW) {
        while (digitalRead(FORCE_PORTAL_PIN) == LOW) delay(10);
        return true;
      }
    }
    delay(10);
  }
  return false;
}

void startPortalAndConnect(bool forcePortal) {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setClass("invert");
  wm.setDarkMode(true);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_SEC);
  if (forcePortal) wm.resetSettings();

  char mqttPortBuf[8];
  char promPortBuf[8];
  snprintf(mqttPortBuf, sizeof(mqttPortBuf), "%u", config.mqttPort);
  snprintf(promPortBuf, sizeof(promPortBuf), "%u", config.prometheusPort);

  WiFiManagerParameter pMqttHost("mqtthost", "MQTT broker", config.mqttHost, sizeof(config.mqttHost));
  WiFiManagerParameter pMqttPort("mqttport", "MQTT port", mqttPortBuf, sizeof(mqttPortBuf));
  WiFiManagerParameter pBaseTopic("basetopic", "Base topic", config.baseTopic, sizeof(config.baseTopic));
  WiFiManagerParameter pDeviceId("deviceid", "Device ID", config.deviceId, sizeof(config.deviceId));
  WiFiManagerParameter pPromPort("prometheusport", "Prometheus port", promPortBuf, sizeof(promPortBuf));

  wm.addParameter(&pMqttHost);
  wm.addParameter(&pMqttPort);
  wm.addParameter(&pBaseTopic);
  wm.addParameter(&pDeviceId);
  wm.addParameter(&pPromPort);

  portalActive = true;
  bool ok;
  if (forcePortal) ok = wm.startConfigPortal("UHFModSetup");
  else ok = wm.autoConnect("UHFModSetup");
  portalActive = false;

  if (!ok) {
    setStatusMessage("portal timeout", 2000);
    delay(1000);
    ESP.restart();
    delay(1000);
  }

  strlcpy(config.mqttHost, pMqttHost.getValue(), sizeof(config.mqttHost));
  strlcpy(config.baseTopic, pBaseTopic.getValue(), sizeof(config.baseTopic));
  strlcpy(config.deviceId, pDeviceId.getValue(), sizeof(config.deviceId));
  if (!config.deviceId[0]) strlcpy(config.deviceId, "uhf_modulator", sizeof(config.deviceId));
  config.mqttPort = uint16_t(atoi(pMqttPort.getValue()));
  if (!config.mqttPort) config.mqttPort = 1883;
  config.prometheusPort = uint16_t(atoi(pPromPort.getValue()));
  if (!config.prometheusPort) config.prometheusPort = 9111;
  buildTopics();
  if (shouldSaveConfig) {
    saveConfig();
    shouldSaveConfig = false;
  }
  setStatusMessage("ip " + ipToString(WiFi.localIP()), 3000);
}

void drawSpinner(int cx, int cy, uint8_t frame) {
  const int dx[8] = {0, 4, 6, 4, 0, -4, -6, -4};
  const int dy[8] = {-6, -4, 0, 4, 6, 4, 0, -4};
  for (int i = 0; i < 8; i++) {
    int idx = (i + frame) & 0x07;
    int px = cx + dx[idx];
    int py = cy + dy[idx];
    if (i < 6) display.fillCircle(px, py, 2, SSD1306_WHITE);
    else if (i < 7) display.drawCircle(px, py, 1, SSD1306_WHITE);
    else display.drawPixel(px, py, SSD1306_WHITE);
  }
}

void renderDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  String hdr = safeDeviceId();
  if (hdr.length() > 15) hdr = hdr.substring(0, 15);
  display.print(hdr);
  drawSpinner(121, 7, spinnerFrame);

  display.setCursor(0, 10);
  display.print("MQTT ");
  display.print(mqtt.connected() ? "up" : "down");
  display.setCursor(64, 10);
  display.print(lastRssi);
  display.print("dBm");

  display.setCursor(0, 22);
  display.print("Profiles ");
  display.print(profileCount);
  display.setCursor(64, 22);
  display.print("Codes ");
  display.print(codeCount);

  display.setCursor(0, 34);
  display.print("LastTX ");
  String lastId(lastTxId);
  if (lastId.length() > 10) lastId = lastId.substring(0, 10);
  display.print(lastId);

  display.setCursor(0, 46);
  display.print("Busy ");
  display.print(txBusy ? "yes" : "no");
  display.setCursor(64, 46);
  display.print("Cnt ");
  display.print(txCount);

  display.setCursor(0, 57);
  String msg = lastStatusMsg;
  if (msg.length() > 21) msg = msg.substring(0, 21);
  display.print(msg);
  display.display();
}

String prometheusEscaped(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (uint16_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

String buildPrometheusMetrics() {
  String idLabel = prometheusEscaped(safeDeviceId());
  metricsScrapeCount++;
  String m;
  m.reserve(4096);
  m += "# HELP uhf_node_online Node online state.\n# TYPE uhf_node_online gauge\nuhf_node_online{id=\"" + idLabel + "\"} 1\n";
  m += "# HELP uhf_wifi_connected WiFi connected state.\n# TYPE uhf_wifi_connected gauge\nuhf_wifi_connected{id=\"" + idLabel + "\"} ";
  m += (WiFi.status() == WL_CONNECTED ? "1\n" : "0\n");
  m += "# HELP uhf_mqtt_connected MQTT connected state.\n# TYPE uhf_mqtt_connected gauge\nuhf_mqtt_connected{id=\"" + idLabel + "\"} ";
  m += (mqtt.connected() ? "1\n" : "0\n");
  m += "# HELP uhf_wifi_rssi_dbm WiFi RSSI in dBm.\n# TYPE uhf_wifi_rssi_dbm gauge\nuhf_wifi_rssi_dbm{id=\"" + idLabel + "\"} " + String(WiFi.RSSI()) + "\n";
  m += "# HELP uhf_free_heap_bytes Free heap.\n# TYPE uhf_free_heap_bytes gauge\nuhf_free_heap_bytes{id=\"" + idLabel + "\"} " + String(ESP.getFreeHeap()) + "\n";
  m += "# HELP uhf_profile_count Profile count.\n# TYPE uhf_profile_count gauge\nuhf_profile_count{id=\"" + idLabel + "\"} " + String(profileCount) + "\n";
  m += "# HELP uhf_code_count Code count.\n# TYPE uhf_code_count gauge\nuhf_code_count{id=\"" + idLabel + "\"} " + String(codeCount) + "\n";
  m += "# HELP uhf_tx_count Total transmissions.\n# TYPE uhf_tx_count counter\nuhf_tx_count{id=\"" + idLabel + "\"} " + String(txCount) + "\n";
  m += "# HELP uhf_mqtt_publish_total Total successful MQTT publishes.\n# TYPE uhf_mqtt_publish_total counter\nuhf_mqtt_publish_total{id=\"" + idLabel + "\"} " + String(mqttPublishCount) + "\n";
  m += "# HELP uhf_prometheus_scrape_total Prometheus scrapes.\n# TYPE uhf_prometheus_scrape_total counter\nuhf_prometheus_scrape_total{id=\"" + idLabel + "\"} " + String(metricsScrapeCount) + "\n";
  return m;
}

void startMetricsServer() {
  if (metricsServerStarted) return;
  if (metricsServer) {
    metricsServer->stop();
    delete metricsServer;
    metricsServer = nullptr;
  }
  metricsServer = new ESP8266WebServer(config.prometheusPort);
  metricsServer->on("/metrics", []() {
    metricsServer->send(200, "text/plain; version=0.0.4; charset=utf-8", buildPrometheusMetrics());
  });
  metricsServer->on("/", []() {
    metricsServer->send(200, "text/plain; charset=utf-8", "uhf_modulator metrics available at /metrics\n");
  });
  metricsServer->begin();
  metricsServerStarted = true;
}

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(TX_PIN, OUTPUT);
  digitalWrite(TX_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.display();

  setStatusMessage("booting", 1500);

  if (!LittleFS.begin()) LittleFS.begin();
  loadConfig();
  buildTopics();
  loadProfiles();
  loadCodes();

  bool forcePortal = startupReconfigRequested();
  startPortalAndConnect(forcePortal);
  setupTime();
  startMetricsServer();
  mqtt.setBufferSize(MQTT_BUFFER_SIZE);
  bootMillis = millis();
  lastDisplayMs = millis();
  lastStatusHeartbeatMs = millis();
  lastRssi = WiFi.RSSI();
  setStatusMessage("uhf ready", 2000);
}

void loop() {
  ensureWiFi();
  if (metricsServerStarted && metricsServer) metricsServer->handleClient();

  unsigned long now = millis();
  if (!mqtt.connected()) {
    mqttOnlinePublished = false;
    if (now - lastMqttAttemptMs >= MQTT_RETRY_MS) {
      lastMqttAttemptMs = now;
      mqttConnect();
    }
  } else {
    mqtt.loop();
  }

  if (mqtt.connected() && !mqttOnlinePublished) {
    publishStatus(true);
    mqttOnlinePublished = true;
  }

  if (mqtt.connected() && now - lastStatusHeartbeatMs >= STATUS_HEARTBEAT_INTERVAL_MS) {
    lastStatusHeartbeatMs = now;
    publishStatus(false);
  }

  if (mqttTrafficActive && now - lastTrafficAnimMs >= 1500UL) mqttTrafficActive = false;

  if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = now;
    lastRssi = WiFi.RSSI();
    spinnerFrame = (spinnerFrame + 1) & 0x07;
    if (!portalActive) renderDisplay();
  }
}
