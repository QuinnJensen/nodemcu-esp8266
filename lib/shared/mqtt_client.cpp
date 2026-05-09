// mqtt_client.cpp -- pure MQTT transport, fully non-blocking reconnect
#include "mqtt_client.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "util.h"

static MqttPublishLogger sPublishLogger = nullptr;
void setMqttPublishLogger(MqttPublishLogger fn) { sPublishLogger = fn; }

// ── reconnect state machine ───────────────────────────────────────────────────
enum class MqttReconnectState : uint8_t {
  IDLE,        // waiting for retry window
  RESOLVING,   // DNS lookup (returns quickly on ESP8266)
  CONNECTING,  // TCP SYN sent; polling wifiClient.connected()
  HANDSHAKING, // TCP up; running PubSubClient MQTT handshake
};

static MqttReconnectState sState           = MqttReconnectState::IDLE;
static unsigned long      sStateEnteredMs  = 0;
static IPAddress          sResolvedIp;

// TCP connect timeout -- we set the WiFiClient timeout to 0 so the initial 
// connect() call returns immediately while LWIP performs the SYN in the 
// background. We then poll for completion in the state machine.
static const unsigned long mqttTcpPollTimeoutMs = 5000UL;

// ── display / handler callbacks ──────────────────────────────────────────────
static MqttClientDisplay    sDisplay;
static MqttMessageHandler   sMessageHandler   = nullptr;
static MqttConnectedHandler sConnectedHandler = nullptr;

void setMqttClientDisplayCallbacks(const MqttClientDisplay& cb) { sDisplay = cb; }
void setMqttMessageHandler(MqttMessageHandler handler)          { sMessageHandler   = handler; }
void setMqttConnectedHandler(MqttConnectedHandler handler)      { sConnectedHandler = handler; }

static void enterIdle() {
  sState          = MqttReconnectState::IDLE;
  sStateEnteredMs = millis();
  lastMqttAttemptMs = millis();
}

void notifyMqttConfigChanged() {
  Serial.println("[MQTT] Config change notified -> forcing reconnect");
  mqtt.disconnect();
  wifiClient.stop();
  enterIdle();
  // Force immediate retry by backdating the attempt timer
  lastMqttAttemptMs = millis() - mqttretryms - 100;
}

const char* mqttReconnectStateLabel() {
  switch (sState) {
    case MqttReconnectState::IDLE:        return "idle";
    case MqttReconnectState::RESOLVING:   return "resolving";
    case MqttReconnectState::CONNECTING:  return "connecting";
    case MqttReconnectState::HANDSHAKING: return "handshaking";
  }
  return "?";
}

// ── helpers ───────────────────────────────────────────────────────────────────
static void _kickSpinner(unsigned long durationMs = 1500) {
  if (sDisplay.kickSpinner) sDisplay.kickSpinner(durationMs);
}
static void _setStatus(const String& msg, unsigned long holdMs = 3000) {
  if (sDisplay.setStatus) sDisplay.setStatus(msg, holdMs);
}

// ── publish helper (unchanged) ────────────────────────────────────────────────
static bool serializeDocToBuffer(const JsonDocument& doc, char* buf, size_t sz, size_t& outLen) {
  outLen = 0;
  if (!buf || sz == 0) return false;
  size_t needed = measureJson(doc);
  if (needed >= sz) {
    Serial.print("[MQTT] payload too large, need="); Serial.print(needed);
    Serial.print(" buf="); Serial.println(sz);
    return false;
  }
  outLen = serializeJson(doc, buf, sz);
  if (outLen == 0) { Serial.println("[MQTT] serializeJson empty"); return false; }
  return true;
}

bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained) {
  if (!mqtt.connected() || !topic || !topic[0]) return false;
  char buffer[mqttbuffersize];
  size_t n = 0;
  if (!serializeDocToBuffer(doc, buffer, sizeof(buffer), n)) {
    Serial.print("[MQTT] publish skipped: "); Serial.println(topic);
    if (sPublishLogger) sPublishLogger(topic, "<serialize failed>", 18, false);
    return false;
  }
  bool ok = mqtt.publish(topic, reinterpret_cast<const uint8_t*>(buffer), n, retained);
  if (ok) { mqttPublishCount++; _kickSpinner(); }
  else { Serial.print("[MQTT] publish failed: "); Serial.println(topic); }
  if (sPublishLogger) sPublishLogger(topic, buffer, n, ok);
  return ok;
}

// ── MQTT message callback ─────────────────────────────────────────────────────
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += char(payload[i]);
  lastRxRaw = msg;
  if (sMessageHandler) sMessageHandler(String(topic), msg);
  else lastRxType = "unhandled";
  _kickSpinner();
}

// ── HANDSHAKING: PubSubClient MQTT connect over already-open TCP socket ───────
static bool runMqttHandshake() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(mqttbuffersize);

  String clientId = "temp-network-" + sanitizeTopicPart(safeDeviceId()) +
                    "-" + String(ESP.getChipId(), HEX);

  StaticJsonDocument<160> willDoc;
  willDoc["type"]   = "status";
  willDoc["id"]     = safeDeviceId();
  willDoc["online"] = false;
  char willPayload[160];
  size_t willLen = 0;
  if (!serializeDocToBuffer(willDoc, willPayload, sizeof(willPayload), willLen)) {
    _setStatus("will too large", 2000);
    return false;
  }

  Serial.print("[MQTT] handshake clientId="); Serial.println(clientId);

  // mqtt.connect() is fast here -- TCP socket is already open
  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                         statusTopic, 0, true, willPayload);
  Serial.print("[MQTT] handshake result="); Serial.println(ok ? "ok" : "fail");
  Serial.print("[MQTT] state="); Serial.println(mqtt.state());

  if (!ok) { _setStatus("broker conn fail", 2000); return false; }

  bool subOk = mqtt.subscribe(commandTopic);
  Serial.print("[MQTT] subscribe="); Serial.println(subOk ? "ok" : "failed");
  if (!subOk) { _setStatus("cmd sub failed", 2000); mqtt.disconnect(); return false; }

  _setStatus("broker connected", 2000);
  if (sConnectedHandler) sConnectedHandler();
  return true;
}

// ── public API ────────────────────────────────────────────────────────────────
void initMqttClient() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(mqttbuffersize);
  enterIdle();
}

void startMqttIfWifiReady() {
  if (WiFi.status() == WL_CONNECTED)
    mqtt.setServer(config.mqttHost, config.mqttPort);
}

void serviceMqttClient() {
  if (WiFi.status() != WL_CONNECTED) return;

  // ── already connected: just pump PubSubClient ────────────────────────────
  if (mqtt.connected()) {
    sState = MqttReconnectState::IDLE; // reset so next disconnect starts fresh
    mqttOnlinePublished = true;        // guard: stays true while connected
    mqtt.loop();
    return;
  }

  // ── not connected: run the state machine ─────────────────────────────────
  mqttOnlinePublished = false;
  unsigned long now = millis();

  switch (sState) {

    // ── IDLE: wait for retry interval ──────────────────────────────────────
    case MqttReconnectState::IDLE:
      if (now - lastMqttAttemptMs >= mqttretryms) {
        Serial.print("[MQTT] begin reconnect -> RESOLVING host=");
        Serial.println(config.mqttHost);
        _setStatus("connecting brkr", 2000);
        sState = MqttReconnectState::RESOLVING;
        sStateEnteredMs = now;
      }
      break;

    // ── RESOLVING: DNS lookup ───────────────────────────────────────────────
    case MqttReconnectState::RESOLVING: {
      IPAddress ip;
      int res = WiFi.hostByName(config.mqttHost, ip);
      if (res == 1) {
        sResolvedIp = ip;
        Serial.print("[MQTT] resolved "); Serial.print(config.mqttHost);
        Serial.print(" -> "); Serial.println(ip);
        
        // Initiate TCP connect with 0 timeout. This makes connect() 
        // return immediately while the SYN happens in the background.
        wifiClient.setTimeout(0);
        wifiClient.connect(sResolvedIp, config.mqttPort);
        
        sState = MqttReconnectState::CONNECTING;
        sStateEnteredMs = now;
        Serial.println("[MQTT] TCP SYN sent -> CONNECTING (background)");
      } else {
        Serial.print("[MQTT] DNS failed for "); Serial.println(config.mqttHost);
        _setStatus("DNS fail", 2000);
        enterIdle();
      }
      break;
    }

    // ── CONNECTING: poll until TCP up or timeout ────────────────────────────
    case MqttReconnectState::CONNECTING:
      if (wifiClient.connected()) {
        Serial.println("[MQTT] TCP connected -> HANDSHAKING");
        // Restore a reasonable timeout for actual data exchange
        wifiClient.setTimeout(5000);
        sState = MqttReconnectState::HANDSHAKING;
        sStateEnteredMs = now;
      } else if (now - sStateEnteredMs >= mqttTcpPollTimeoutMs) {
        Serial.println("[MQTT] TCP connect timeout");
        _setStatus("broker timeout", 2000);
        wifiClient.stop();
        enterIdle();
      }
      break;

    // ── HANDSHAKING: MQTT protocol over open TCP socket ─────────────────────
    case MqttReconnectState::HANDSHAKING:
      if (runMqttHandshake()) {
        sState = MqttReconnectState::IDLE; // connected; next disconnect restarts
      } else {
        wifiClient.stop();
        enterIdle();
      }
      break;
  }
}
