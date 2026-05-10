// uhf_mqtt.cpp - command dispatcher and status publish
#include "uhf_mqtt.h"
#include "uhf_codec.h"
#include <ESP8266WiFi.h>
#include <math.h>
#include "app_state.h"
#include "app_config.h"
#include "mqtt_client.h"
#include "util.h"
#include "console_log.h"
#include "display_ui.h"
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include "sensor_names.h"
#endif

bool pendingScan = false;

// Deferred-tx queue (single slot is sufficient — TX runs to completion in the
// dispatcher; we just want to defer it OUT of the MQTT callback).
static bool pendingTxCode = false;
static char pendingTxCodeId[UHF_ID_LEN];
static bool pendingTxRaw = false;
static char pendingTxRawProfileId[UHF_ID_LEN];
static uint32_t pendingTxRawValue = 0;
static uint16_t pendingTxRawBits = 0;
static uint16_t pendingTxRawRepeat = 0;

void publishCommandResult(const char* type, bool ok, const char* message) {
  if (!mqtt.connected()) return;
  DynamicJsonDocument reply(512);
  reply["type"] = type;
  reply["id"]   = safeDeviceId();
  reply["ok"]   = ok;
  reply["message"] = message ? message : "";
  publishJsonDocToTopic(resultsTopic, reply, false);
}

void publishUhfStatus(bool retained) {
  if (!mqtt.connected()) return;
  DynamicJsonDocument doc(2560);
  doc["type"] = "status";
  doc["id"]   = safeDeviceId();
  doc["online"] = true;
  doc["build_version"] = buildVersion;
  doc["ssid"] = WiFi.SSID();
  doc["rssidbm"] = WiFi.RSSI();
  doc["ip"] = ipToString(WiFi.localIP());
  doc["freeheap"] = ESP.getFreeHeap();
  doc["uptime_s"] = millis() / 1000UL;
  doc["mqttpublishcount"] = mqttPublishCount;
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
  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;
#ifdef SHARED_LIB_USE_ONEWIRE
  doc["sensorcount"] = sensorCount;
  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"] = i + 1;
    s["name"]  = sensorNames[i];
    s["address"] = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (!isnan(sensorTempsC[i])) {
      s["tempc"] = sensorTempsC[i];
      s["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
    }
  }
#endif
  publishJsonDocToTopic(statusTopic, doc, retained);
}

void handleCommandJson(const String& payload) {
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    lastRxType = "badjson";
    publishCommandResult("error", false, "bad json");
    setStatusMessage("bad json cmd", 2500);
    consoleLog(CLOG_WARN, ("[RX] Bad JSON: " + payload).c_str());
    return;
  }
  const char* command = doc["command"] | doc["type"] | "";
  lastRxType = String(command);
  consoleLog(CLOG_RX, ("[RX] command: " + String(command)).c_str());

  if (!strcmp(command, "status") || !strcmp(command, "heartbeat")) {
    publishUhfStatus(false);
    setStatusMessage("publishing status", 1500);
    return;
  }
  if (!strcmp(command, "listprofiles")) {
    DynamicJsonDocument reply(4096);
    reply["type"] = "listprofilesreply";
    reply["id"]   = safeDeviceId();
    reply["ok"]   = true;
    appendProfilesToJson(reply);
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }
  if (!strcmp(command, "listcodes")) {
    DynamicJsonDocument reply(6144);
    reply["type"] = "listcodesreply";
    reply["id"]   = safeDeviceId();
    reply["ok"]   = true;
    appendCodesToJson(reply);
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }
  if (!strcmp(command, "getprofile")) {
    const char* id = doc["id"] | "";
    int idx = findProfileIndexById(id);
    if (idx < 0) { publishCommandResult("getprofilereply", false, "profile not found"); return; }
    DynamicJsonDocument reply(1024);
    reply["type"] = "getprofilereply";
    reply["id"]   = safeDeviceId();
    reply["ok"]   = true;
    JsonObject p = reply.createNestedObject("profile");
    p["id"]=profiles[idx].id; p["pulse_us"]=profiles[idx].pulseUs;
    p["sync_high"]=profiles[idx].syncHigh; p["sync_low"]=profiles[idx].syncLow;
    p["zero_high"]=profiles[idx].zeroHigh; p["zero_low"]=profiles[idx].zeroLow;
    p["one_high"]=profiles[idx].oneHigh;   p["one_low"]=profiles[idx].oneLow;
    p["default_bits"]=profiles[idx].defaultBits; p["default_repeat"]=profiles[idx].defaultRepeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }
  if (!strcmp(command, "getcode")) {
    const char* id = doc["id"] | "";
    int idx = findCodeIndexById(id);
    if (idx < 0) { publishCommandResult("getcodereply", false, "code not found"); return; }
    DynamicJsonDocument reply(1024);
    reply["type"]="getcodereply"; reply["id"]=safeDeviceId(); reply["ok"]=true;
    JsonObject c = reply.createNestedObject("code");
    c["id"]=codes[idx].id; c["profileId"]=codes[idx].profileId;
    c["value"]=codes[idx].value; c["bits"]=codes[idx].bits; c["repeat"]=codes[idx].repeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }
  if (!strcmp(command, "upsertprofile")) {
    JsonObjectConst pObj = doc["profile"].as<JsonObjectConst>();
    TimingProfile p;
    if (!pObj.isNull() && profileFromJson(pObj, p) && upsertProfile(p, true))
      publishCommandResult("upsertprofilereply", true, "profile saved");
    else publishCommandResult("upsertprofilereply", false, "invalid profile or save failed");
    return;
  }
  if (!strcmp(command, "upsertcode")) {
    JsonObjectConst cObj = doc["code"].as<JsonObjectConst>();
    CodeRecord c;
    if (!cObj.isNull() && codeFromJson(cObj, c) && upsertCode(c, true))
      publishCommandResult("upsertcodereply", true, "code saved");
    else publishCommandResult("upsertcodereply", false, "invalid code or save failed");
    return;
  }
  if (!strcmp(command, "replaceprofiles")) {
    JsonArrayConst arr = doc["profiles"].as<JsonArrayConst>();
    if (arr.isNull() || arr.size() > UHF_MAX_PROFILES) {
      publishCommandResult("replaceprofilesreply", false, "invalid profiles array"); return;
    }
    TimingProfile temp[UHF_MAX_PROFILES];
    uint8_t count = 0;
    for (JsonObjectConst obj : arr) {
      TimingProfile p;
      if (!profileFromJson(obj, p)) {
        publishCommandResult("replaceprofilesreply", false, "invalid profile entry"); return;
      }
      temp[count++] = p;
    }
    for (uint8_t i = 0; i < codeCount; i++) {
      bool found = false;
      for (uint8_t j = 0; j < count; j++) if (!strcmp(codes[i].profileId, temp[j].id)) found = true;
      if (!found) { publishCommandResult("replaceprofilesreply", false, "existing code would reference missing profile"); return; }
    }
    clearProfiles();
    for (uint8_t i = 0; i < count; i++) profiles[profileCount++] = temp[i];
    if (saveProfiles()) publishCommandResult("replaceprofilesreply", true, "profiles replaced");
    else publishCommandResult("replaceprofilesreply", false, "save failed");
    return;
  }
  if (!strcmp(command, "replacecodes")) {
    JsonArrayConst arr = doc["codes"].as<JsonArrayConst>();
    if (arr.isNull() || arr.size() > UHF_MAX_CODES) {
      publishCommandResult("replacecodesreply", false, "invalid codes array"); return;
    }
    CodeRecord temp[UHF_MAX_CODES];
    uint8_t count = 0;
    for (JsonObjectConst obj : arr) {
      CodeRecord c;
      if (!codeFromJson(obj, c)) { publishCommandResult("replacecodesreply", false, "invalid code entry"); return; }
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
    if (findCodeIndexById(id) < 0) { publishCommandResult("txreply", false, "code not found"); return; }
    strlcpy(pendingTxCodeId, id, sizeof(pendingTxCodeId));
    pendingTxCode = true;
    setStatusMessage("tx queued", 1500);
    return;
  }
  if (!strcmp(command, "txraw")) {
    const char* profileId = doc["profileId"] | "";
    int pidx = findProfileIndexById(profileId);
    if (pidx < 0) { publishCommandResult("txrawreply", false, "profile not found"); return; }
    strlcpy(pendingTxRawProfileId, profileId, sizeof(pendingTxRawProfileId));
    pendingTxRawValue = doc["value"] | 0;
    pendingTxRawBits  = doc["bits"]  | profiles[pidx].defaultBits;
    pendingTxRawRepeat= doc["repeat"]| profiles[pidx].defaultRepeat;
    pendingTxRaw = true;
    setStatusMessage("tx raw queued", 1500);
    return;
  }
  if (!strcmp(command, "ls")) {
    DynamicJsonDocument reply(1024);
    reply["type"]="lsreply"; reply["id"]=safeDeviceId(); reply["ok"]=true;
    reply["profilesfile"]=UHF_PROFILES_FILE;
    reply["codesfile"]=UHF_CODES_FILE;
    reply["commandtopic"]=commandTopic;
    reply["statustopic"]=statusTopic;
    reply["resultstopic"]=resultsTopic;
    reply["profilecount"]=profileCount;
    reply["codecount"]=codeCount;
    publishJsonDocToTopic(resultsTopic, reply, false);
    return;
  }
#ifdef SHARED_LIB_USE_ONEWIRE
  if (!strcmp(command, "scan") || !strcmp(command, "temps")) {
    pendingScan = true;
    setStatusMessage("scan queued", 1500);
    return;
  }
#endif
  publishCommandResult("error", false, "unknown command");
  consoleLog(CLOG_WARN, ("[CMD] unknown: " + String(command)).c_str());
}

void serviceUhfDeferred() {
  if (pendingTxCode) {
    pendingTxCode = false;
    int idx = findCodeIndexById(pendingTxCodeId);
    if (idx < 0) { publishCommandResult("txreply", false, "code not found"); return; }
    bool ok = transmitCodeRecord(codes[idx]);
    DynamicJsonDocument reply(1024);
    reply["type"]="txreply"; reply["id"]=safeDeviceId(); reply["ok"]=ok;
    reply["codeId"]=codes[idx].id; reply["profileId"]=codes[idx].profileId;
    reply["value"]=codes[idx].value; reply["bits"]=codes[idx].bits; reply["repeat"]=codes[idx].repeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    publishUhfStatus(false);
  }
  if (pendingTxRaw) {
    pendingTxRaw = false;
    int pidx = findProfileIndexById(pendingTxRawProfileId);
    if (pidx < 0) { publishCommandResult("txrawreply", false, "profile not found"); return; }
    bool ok = transmitWaveform(profiles[pidx], pendingTxRawValue, pendingTxRawBits, pendingTxRawRepeat);
    DynamicJsonDocument reply(1024);
    reply["type"]="txrawreply"; reply["id"]=safeDeviceId(); reply["ok"]=ok;
    reply["profileId"]=profiles[pidx].id;
    reply["value"]=pendingTxRawValue; reply["bits"]=pendingTxRawBits; reply["repeat"]=pendingTxRawRepeat;
    publishJsonDocToTopic(resultsTopic, reply, false);
    publishUhfStatus(false);
  }
}
