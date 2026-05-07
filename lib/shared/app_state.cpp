#include "app_state.h"
#include <Wire.h>

Adafruit_SSD1306 display(screenwidth, screenheight, &Wire, oledreset);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
OneWire oneWire(oneWirePin);
DallasTemperature ds(&oneWire);

ESP8266WebServer webServer(80);
ESP8266WebServer* metricsServer = nullptr;

DeviceAddress sensorAddresses[maxsensors];
float sensorTempsC[maxsensors];
bool sensorPresent[maxsensors];
char sensorNames[maxsensors][sensornamelen];
uint8_t sensorCount = 0;
bool sensorNetworkDetected = false;
bool useFakeSensors = false;
bool everHadPhysicalSensors = false;

SensorNameRecord sensorNameRecords[maxsensors];
uint8_t sensorNameRecordCount = 0;

bool shouldSaveConfig = false;
bool portalActive = false;
bool mqttOnlinePublished = false;
bool timeConfigured = false;

unsigned long lastAggregateHeartbeatMs = 0;
unsigned long lastSensorHeartbeatMs = 0;
unsigned long lastWaterHeartbeatMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastTrafficAnimMs = 0;
unsigned long statusMsgUntilMs = 0;
unsigned long lastSensorSampleMs = 0;
unsigned long lastSensorRescanMs = 0;
unsigned long lastWaterSampleMs = 0;
unsigned long bootMillis = 0;
unsigned long mqttPublishCount = 0;
unsigned long metricsScrapeCount = 0;

int lastRssi = -127;
bool mqttTrafficActive = false;
uint8_t spinnerFrame = 0;
uint8_t displayStartSensor = 0;
String lastRxType = "-";
String lastStatusMsg = "booting";
String lastRxRaw = "";

uint16_t waterAdcRaw = 0;
uint8_t waterLevelIndex = WATER_UNKNOWN;
bool waterValid = false;
bool waterProbePresent = false;

bool startupDisplayActive = true;
unsigned long startupDisplayUntilMs = 0;

volatile bool webRequestSensorScan = false;
volatile bool webRequestWaterSample = false;
