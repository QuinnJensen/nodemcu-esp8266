/*
  ESP8266 Water Heater UI + WiFiManager + MQTT + OLED + Status Messages
  + 120 Hz Bresenham SSR Simulation Output (scope/LED testing only)
  + LittleFS ls command over MQTT
  + Separate results topic for ls_reply
  + Hard-coded US Mountain time heartbeat timestamps
  + Power calibration table with interpolation/extrapolation

  MQTT commands:
    {"type":"command","power_percent":37}
    {"type":"ls"}
    {"type":"calibrate","actual_power_watts":612.0}
    {"type":"purge_calibration"}

  Notes:
    - Calibration table is 10 entries for 10%,20%,...,100%.
    - Each entry is actual watts, or -1.0 if not yet recorded.
    - 0% is always assumed to be 0 watts.
    - At least one calibration point is required to enable correction.
    - If no calibration points exist, nominal full-scale model is used.
    - Calibrate uses the CURRENT applied requested_power_pct.
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
#include <math.h>

extern "C" {
  #include "user_interface.h"
}

#define screen_width 128
#define screen_height 64
#define oled_reset   -1
#define oled_addr    0x3C

#define i2c_sda D5
#define i2c_scl D6

#define force_portal_pin D3
#define ssr_sim_pin D7
#define config_file "/config.json"
#define cal_file "/cal.json"

#define heartbeat_interval_ms 15000UL
#define display_interval_ms   100UL
#define mqtt_retry_ms         3000UL
#define portal_timeout_sec    180
#define full_scale_watts      1800.0f
#define nominal_vrms          120.0f
#define spinner_active_ms     1500UL

#define modulator_hz 120UL
#define timer1_divider TIM_DIV16
#define timer1_ticks ((5000000UL / modulator_hz) - 1)

#define mqtt_buffer_size 1536
#define device_tz "MST7MDT,M3.2.0,M11.1.0"

#define cal_points 10

Adafruit_SSD1306 display(screen_width, screen_height, &Wire, oled_reset);
WiFiClient wifi_client;
PubSubClient mqtt(wifi_client);

struct app_config_t {
  char mqtt_host[64]  = "192.168.1.50";
  char base_topic[64] = "mountain/water_heater";
  uint16_t mqtt_port  = 1883;
} config;

float cal_table[cal_points];

char command_topic[96];
char status_topic[96];
char results_topic[96];

bool should_save_config = false;
bool portal_active = false;
bool mqtt_online_published = false;
bool time_configured = false;

bool pending_ls_request = false;
bool pending_calibration_request = false;
bool pending_purge_calibration_request = false;
float pending_actual_power_watts = 0.0f;

unsigned long last_heartbeat_ms = 0;
unsigned long last_display_ms = 0;
unsigned long last_mqtt_attempt_ms = 0;
unsigned long last_command_ms = 0;
unsigned long last_traffic_anim_ms = 0;
unsigned long status_msg_until_ms = 0;
unsigned long power_level_changed_ms = 0;

int requested_power_pct = 0;
int prior_power_pct = 0;
int displayed_power_watts = 0;
int last_rssi = -127;

bool mqtt_traffic_active = false;
uint8_t spinner_frame = 0;
String last_rx_type = "-";
String last_status_msg = "booting";
String last_rx_raw = "";

/* shared with isr */
volatile uint8_t isr_power_pct = 0;
volatile uint8_t isr_output_state = 0;
volatile uint16_t bres_acc = 0;
volatile uint32_t sim_tick_count = 0;
volatile uint32_t sim_on_tick_count = 0;

void set_status_message(const String& msg, unsigned long hold_ms = 3000);
void refresh_displayed_power();

void build_topics() {
  snprintf(command_topic, sizeof(command_topic), "%s/command", config.base_topic);
  snprintf(status_topic, sizeof(status_topic), "%s/status", config.base_topic);
  snprintf(results_topic, sizeof(results_topic), "%s/results", config.base_topic);
}

void clear_calibration_table() {
  for (int i = 0; i < cal_points; i++) {
    cal_table[i] = -1.0f;
  }
}

bool has_any_calibration() {
  for (int i = 0; i < cal_points; i++) {
    if (cal_table[i] >= 0.0f) return true;
  }
  return false;
}

int cal_index_from_percent(int pct) {
  pct = constrain(pct, 10, 100);
  int idx = (pct / 10) - 1;
  if (idx < 0) idx = 0;
  if (idx >= cal_points) idx = cal_points - 1;
  return idx;
}

int cal_percent_from_index(int idx) {
  return (idx + 1) * 10;
}

float nominal_watts_for_percent(int pct) {
  pct = constrain(pct, 0, 100);
  return (full_scale_watts * pct) / 100.0f;
}

float interpolate_segment(int x, int x0, float y0, int x1, float y1) {
  if (x1 == x0) return y0;
  return y0 + ((float)(x - x0) * (y1 - y0)) / (float)(x1 - x0);
}

float estimate_corrected_watts(int pct) {
  pct = constrain(pct, 0, 100);
  if (pct == 0) return 0.0f;

  if (!has_any_calibration()) {
    return nominal_watts_for_percent(pct);
  }

  if (pct % 10 == 0) {
    int exact_idx = cal_index_from_percent(pct);
    if (cal_table[exact_idx] >= 0.0f) {
      return cal_table[exact_idx];
    }
  }

  int lower_idx = -1;
  int upper_idx = -1;
  int exact_bucket = pct / 10;

  for (int i = exact_bucket - 1; i >= 0; i--) {
    if (i >= 1 && cal_table[i - 1] >= 0.0f) {
      lower_idx = i - 1;
      break;
    }
  }

  for (int i = (pct + 9) / 10; i <= 10; i++) {
    if (i >= 1 && i <= 10 && cal_table[i - 1] >= 0.0f) {
      upper_idx = i - 1;
      break;
    }
  }

  if (lower_idx >= 0 && upper_idx >= 0 && lower_idx != upper_idx) {
    int x0 = cal_percent_from_index(lower_idx);
    int x1 = cal_percent_from_index(upper_idx);
    float y0 = cal_table[lower_idx];
    float y1 = cal_table[upper_idx];
    return interpolate_segment(pct, x0, y0, x1, y1);
  }

  if (lower_idx >= 0) {
    int x0 = cal_percent_from_index(lower_idx);
    float y0 = cal_table[lower_idx];

    int prev_idx = -1;
    for (int i = lower_idx - 1; i >= 0; i--) {
      if (cal_table[i] >= 0.0f) {
        prev_idx = i;
        break;
      }
    }

    if (prev_idx >= 0) {
      int x_prev = cal_percent_from_index(prev_idx);
      float y_prev = cal_table[prev_idx];
      return interpolate_segment(pct, x_prev, y_prev, x0, y0);
    }

    return interpolate_segment(pct, 0, 0.0f, x0, y0);
  }

  if (upper_idx >= 0) {
    int x1 = cal_percent_from_index(upper_idx);
    float y1 = cal_table[upper_idx];

    int next_idx = -1;
    for (int i = upper_idx + 1; i < cal_points; i++) {
      if (cal_table[i] >= 0.0f) {
        next_idx = i;
        break;
      }
    }

    if (next_idx >= 0) {
      int x2 = cal_percent_from_index(next_idx);
      float y2 = cal_table[next_idx];
      return interpolate_segment(pct, x1, y1, x2, y2);
    }

    return interpolate_segment(pct, 0, 0.0f, x1, y1);
  }

  return nominal_watts_for_percent(pct);
}

float estimate_current_amps(float est_watts) {
  if (nominal_vrms <= 0.0f) return 0.0f;
  return est_watts / nominal_vrms;
}

void refresh_displayed_power() {
  float est_watts = estimate_corrected_watts(requested_power_pct);
  if (est_watts < 0.0f) est_watts = 0.0f;
  displayed_power_watts = (int)lroundf(est_watts);
}

bool load_calibration() {
  clear_calibration_table();

  if (!LittleFS.exists(cal_file)) return false;

  File f = LittleFS.open(cal_file, "r");
  if (!f) return false;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonArray arr = doc["points"].as<JsonArray>();
  if (arr.isNull()) return false;

  for (int i = 0; i < cal_points && i < (int)arr.size(); i++) {
    cal_table[i] = arr[i] | -1.0f;
  }

  return true;
}

bool save_calibration() {
  StaticJsonDocument<256> doc;
  JsonArray arr = doc.createNestedArray("points");

  for (int i = 0; i < cal_points; i++) {
    arr.add(cal_table[i]);
  }

  File f = LittleFS.open(cal_file, "w");
  if (!f) return false;

  serializeJson(doc, f);
  f.close();
  return true;
}

bool purge_calibration_file() {
  clear_calibration_table();

  if (LittleFS.exists(cal_file)) {
    if (!LittleFS.remove(cal_file)) {
      set_status_message("cal purge fail", 2500);
      return false;
    }
  }

  refresh_displayed_power();
  set_status_message("cal purged", 2000);
  return true;
}

void set_status_message(const String& msg, unsigned long hold_ms) {
  last_status_msg = msg;
  status_msg_until_ms = millis() + hold_ms;
  Serial.println(msg);
}

void kick_activity_spinner(unsigned long duration_ms = spinner_active_ms) {
  mqtt_traffic_active = true;
  last_traffic_anim_ms = millis() + duration_ms - spinner_active_ms;
  spinner_frame = (spinner_frame + 1) & 0x07;
}

void save_config_callback() {
  should_save_config = true;
}

bool load_config() {
  if (!LittleFS.begin()) return false;

  if (!LittleFS.exists(config_file)) {
    build_topics();
    return false;
  }

  File f = LittleFS.open(config_file, "r");
  if (!f) {
    build_topics();
    return false;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    build_topics();
    return false;
  }

  strlcpy(config.mqtt_host, doc["mqtt_host"] | "192.168.1.50", sizeof(config.mqtt_host));
  strlcpy(config.base_topic, doc["base_topic"] | "mountain/water_heater", sizeof(config.base_topic));
  config.mqtt_port = doc["mqtt_port"] | 1883;

  build_topics();
  return true;
}

bool save_config() {
  StaticJsonDocument<256> doc;
  doc["mqtt_host"] = config.mqtt_host;
  doc["mqtt_port"] = config.mqtt_port;
  doc["base_topic"] = config.base_topic;

  File f = LittleFS.open(config_file, "w");
  if (!f) return false;

  serializeJson(doc, f);
  f.close();
  return true;
}

void setup_time() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", device_tz, 1);
  tzset();
  time_configured = true;
}

String ip_to_string(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

String time_to_string(time_t t) {
  if (t <= 0) return "";

  struct tm tm_struct;
  localtime_r(&t, &tm_struct);

  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_struct);

  String ts(buf);
  if (ts.length() == 24) {
    ts = ts.substring(0, 22) + ":" + ts.substring(22);
  }
  return ts;
}

String current_timestamp_string() {
  time_t now = time(nullptr);
  if (now <= 100000) return "";
  return time_to_string(now);
}

void draw_spinner(int cx, int cy, uint8_t frame) {
  const int dx[8] = { 0,  4,  6,  4,  0, -4, -6, -4 };
  const int dy[8] = {-6, -4,  0,  4,  6,  4,  0, -4 };

  for (int i = 0; i < 8; i++) {
    int idx = (i + frame) & 0x07;
    int px = cx + dx[idx];
    int py = cy + dy[idx];

    if (i >= 6) {
      display.fillCircle(px, py, 2, SSD1306_WHITE);
    } else if (i >= 3) {
      display.drawCircle(px, py, 1, SSD1306_WHITE);
    } else {
      display.drawPixel(px, py, SSD1306_WHITE);
    }
  }
}

void draw_power_bar(int x, int y, int w, int h, int pct) {
  display.drawRect(x, y, w, h, SSD1306_WHITE);

  int inner_w = w - 2;
  int inner_h = h - 2;
  int fill_w = (inner_w * pct) / 100;

  if (fill_w > 0) {
    display.fillRect(x + 1, y + 1, fill_w, inner_h, SSD1306_WHITE);
  }

  for (int i = 20; i < 100; i += 20) {
    int tx = x + 1 + (inner_w * i) / 100;
    display.drawFastVLine(tx, y + 1, inner_h, SSD1306_BLACK);
  }
}

void show_portal_screen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Setup Portal");
  display.setCursor(0, 12);
  display.print("AP: WaterHeaterSetup");
  display.setCursor(0, 24);
  display.print("Go to 192.168.4.1");
  display.setCursor(0, 50);
  display.print("msg: portal active");
  display.display();
}

void render_display() {
  uint8_t sim_state;
  uint32_t ticks;
  uint32_t on_ticks;

  noInterrupts();
  sim_state = isr_output_state;
  ticks = sim_tick_count;
  on_ticks = sim_on_tick_count;
  interrupts();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print(WiFi.isConnected() ? WiFi.SSID() : "wifi down");

  draw_spinner(121, 7, spinner_frame);

  display.setCursor(0, 10);
  display.print("MQTT:");
  display.print(mqtt.connected() ? "up" : "down");

  display.setCursor(64, 10);
  display.print(last_rssi);
  display.print("dBm");

  display.setCursor(0, 20);
  display.print("P ");
  display.print(requested_power_pct);
  display.print("% ");
  display.print(displayed_power_watts);
  display.print("W");

  draw_power_bar(0, 29, 128, 10, requested_power_pct);

  display.setCursor(0, 42);
  display.print("SIM:");
  display.print(sim_state ? "ON " : "OFF");
  display.print(" T:");
  display.print(ticks % 1000);

  display.setCursor(70, 42);
  display.print("On:");
  display.print(on_ticks % 1000);

  display.drawFastHLine(0, 55, 128, SSD1306_WHITE);
  display.setCursor(0, 57);

  String msg = last_status_msg;
  if (msg.length() > 21) {
    msg = msg.substring(0, 21);
  }
  display.print(msg);

  display.display();
}

void update_power_values(int pct) {
  pct = constrain(pct, 0, 100);

  if (pct != requested_power_pct) {
    prior_power_pct = requested_power_pct;
    requested_power_pct = pct;
    power_level_changed_ms = millis();
  } else {
    requested_power_pct = pct;
  }

  refresh_displayed_power();

  noInterrupts();
  isr_power_pct = requested_power_pct;
  interrupts();
}

// ===== PART 2 below =======================================================================================

bool publish_json_doc_to_topic(const char* topic, const JsonDocument& doc, bool retained = false) {
  if (!mqtt.connected()) return false;

  char buffer[mqtt_buffer_size];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));
  bool ok = mqtt.publish(topic, (const uint8_t*)buffer, n, retained);

  if (ok) {
    kick_activity_spinner();
  }
  return ok;
}

void publish_filesystem_listing() {
  FSInfo fs_info;
  if (!LittleFS.info(fs_info)) {
    StaticJsonDocument<192> err_doc;
    err_doc["type"] = "ls_reply";
    err_doc["ok"] = false;
    err_doc["error"] = "fs_info_failed";
    publish_json_doc_to_topic(results_topic, err_doc, false);
    set_status_message("ls fs info fail", 2000);
    return;
  }

  StaticJsonDocument<mqtt_buffer_size> doc;
  doc["type"] = "ls_reply";
  doc["ok"] = true;

  String ts = current_timestamp_string();
  if (ts.length()) {
    doc["timestamp"] = ts;
  }

  doc["fs_total_bytes"] = fs_info.totalBytes;
  doc["fs_used_bytes"] = fs_info.usedBytes;
  doc["fs_free_bytes"] = fs_info.totalBytes - fs_info.usedBytes;
  doc["fs_block_size"] = fs_info.blockSize;
  doc["fs_page_size"] = fs_info.pageSize;
  doc["max_open_files"] = fs_info.maxOpenFiles;
  doc["max_path_length"] = fs_info.maxPathLength;
  doc["calibration_enabled"] = has_any_calibration();

  JsonArray files = doc.createNestedArray("files");

  Dir dir = LittleFS.openDir("/");
  uint16_t count = 0;

  while (dir.next()) {
    JsonObject entry = files.createNestedObject();
    entry["name"] = dir.fileName();
    entry["size"] = dir.fileSize();

    File f = dir.openFile("r");
    if (f) {
      time_t created_time = f.getCreationTime();
      time_t modified_time = f.getLastWrite();

      String created = time_to_string(created_time);
      String modified = time_to_string(modified_time);

      if (created.length()) entry["created"] = created;
      if (modified.length()) entry["modified"] = modified;
      f.close();
    }
    count++;
  }

  doc["file_count"] = count;

  if (publish_json_doc_to_topic(results_topic, doc, false)) {
    set_status_message("publishing ls", 2000);
  } else {
    set_status_message("ls publish fail", 2000);
  }
}

void publish_status(bool retained = false) {
  if (!mqtt.connected()) return;

  uint8_t sim_state;
  uint32_t ticks;
  uint32_t on_ticks;

  noInterrupts();
  sim_state = isr_output_state;
  ticks = sim_tick_count;
  on_ticks = sim_on_tick_count;
  interrupts();

  float est_watts = estimate_corrected_watts(requested_power_pct);
  float est_amps = estimate_current_amps(est_watts);

  StaticJsonDocument<1024> doc;
  doc["type"] = "status";
  doc["online"] = true;
  doc["ssid"] = WiFi.SSID();
  doc["rssi_dbm"] = WiFi.RSSI();
  doc["ip"] = ip_to_string(WiFi.localIP());

  String ts = current_timestamp_string();
  if (ts.length()) {
    doc["timestamp"] = ts;
  }

  doc["power_percent"] = requested_power_pct;
  doc["prior_power_percent"] = prior_power_pct;
  doc["est_power_watts"] = est_watts;
  doc["est_current_amps"] = est_amps;
  doc["seconds_since_last_command"] = (millis() - last_command_ms) / 1000UL;
  doc["seconds_at_current_power_level"] = (millis() - power_level_changed_ms) / 1000UL;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["sim_output"] = sim_state;
  doc["sim_ticks"] = ticks;
  doc["sim_on_ticks"] = on_ticks;
  doc["calibration_enabled"] = has_any_calibration();

  JsonArray calibration_points = doc.createNestedArray("calibration_points");
  for (int i = 0; i < cal_points; i++) {
    calibration_points.add(cal_table[i]);
  }

  if (publish_json_doc_to_topic(status_topic, doc, retained)) {
    set_status_message("publishing hb", 1500);
  } else {
    set_status_message("publish failed", 2000);
  }
}

void publish_online_status() {
  publish_status(true);
  mqtt_online_published = true;
}

bool handle_calibration_request(float actual_watts) {
  int pct = requested_power_pct;
  if (pct < 10) {
    set_status_message("set pwr >=10", 2500);
    return false;
  }

  if (actual_watts <= 0.0f) {
    set_status_message("bad cal watts", 2500);
    return false;
  }

  int bucket_pct = ((pct + 5) / 10) * 10;
  bucket_pct = constrain(bucket_pct, 10, 100);

  int idx = cal_index_from_percent(bucket_pct);
  cal_table[idx] = actual_watts;

  if (!save_calibration()) {
    set_status_message("cal save fail", 2500);
    return false;
  }

  refresh_displayed_power();
  set_status_message("cal stored", 2000);
  return true;
}

void handle_command_json(const String& payload) {
  StaticJsonDocument<320> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    last_rx_type = "badjson";
    set_status_message("bad json cmd", 2500);
    return;
  }

  const char* type = doc["type"] | "command";
  last_rx_type = String(type);

  if (strcmp(type, "ls") == 0) {
    pending_ls_request = true;
    last_command_ms = millis();
    kick_activity_spinner();
    set_status_message("ls queued", 1500);
    return;
  }

  if (strcmp(type, "calibrate") == 0) {
    if (!doc.containsKey("actual_power_watts")) {
      set_status_message("cal missing watts", 2500);
      return;
    }

    pending_actual_power_watts = doc["actual_power_watts"].as<float>();
    pending_calibration_request = true;
    last_command_ms = millis();
    kick_activity_spinner();
    set_status_message("cal queued", 1500);
    return;
  }

  if (strcmp(type, "purge_calibration") == 0) {
    pending_purge_calibration_request = true;
    last_command_ms = millis();
    kick_activity_spinner();
    set_status_message("purge queued", 1500);
    return;
  }

  if (doc.containsKey("power_percent")) {
    update_power_values(doc["power_percent"].as<int>());
  } else if (doc.containsKey("power")) {
    update_power_values(doc["power"].as<int>());
  }

  last_command_ms = millis();
  kick_activity_spinner();
  set_status_message("cmd received", 2000);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  last_rx_raw = msg;

  if (String(topic) == command_topic) {
    handle_command_json(msg);
  } else {
    last_rx_type = "other";
    kick_activity_spinner();
    set_status_message("msg other topic", 1500);
  }
}

bool mqtt_connect() {
  mqtt.setServer(config.mqtt_host, config.mqtt_port);
  mqtt.setCallback(mqtt_callback);

  String client_id = "water-heater-";
  client_id += String(ESP.getChipId(), HEX);

  StaticJsonDocument<128> will_doc;
  will_doc["type"] = "status";
  will_doc["online"] = false;

  char will_payload[128];
  serializeJson(will_doc, will_payload, sizeof(will_payload));

  set_status_message("connecting brkr", 2000);

  bool ok = mqtt.connect(
    client_id.c_str(),
    nullptr,
    nullptr,
    status_topic,
    0,
    true,
    will_payload
  );

  if (ok) {
    mqtt.subscribe(command_topic);
    publish_online_status();
    set_status_message("broker connected", 2000);
    return true;
  }

  set_status_message("broker conn fail", 2000);
  return false;
}

void ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    last_rssi = WiFi.RSSI();
    return;
  }

  set_status_message("connecting wifi", 2000);
  WiFi.reconnect();

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    last_rssi = WiFi.RSSI();
    set_status_message("ip " + ip_to_string(WiFi.localIP()), 3000);
  }
}

void start_portal_and_connect(bool force_portal) {
  WiFiManager wm;
  wm.setSaveConfigCallback(save_config_callback);
  wm.setConfigPortalTimeout(portal_timeout_sec);

  char mqtt_port_buf[8];
  snprintf(mqtt_port_buf, sizeof(mqtt_port_buf), "%u", config.mqtt_port);

  WiFiManagerParameter p_mqtt_host("mqtt_host", "MQTT broker", config.mqtt_host, sizeof(config.mqtt_host));
  WiFiManagerParameter p_mqtt_port("mqtt_port", "MQTT port", mqtt_port_buf, sizeof(mqtt_port_buf));
  WiFiManagerParameter p_base_topic("base_topic", "Base topic", config.base_topic, sizeof(config.base_topic));

  wm.addParameter(&p_mqtt_host);
  wm.addParameter(&p_mqtt_port);
  wm.addParameter(&p_base_topic);

  portal_active = true;
  show_portal_screen();

  bool ok;
  if (force_portal) {
    set_status_message("forced portal", 3000);
    ok = wm.startConfigPortal("WaterHeaterSetup");
  } else {
    set_status_message("connecting wifi", 2000);
    ok = wm.autoConnect("WaterHeaterSetup");
  }

  portal_active = false;

  if (!ok) {
    set_status_message("portal timeout", 2000);
    delay(1000);
    ESP.restart();
    delay(1000);
  }

  strlcpy(config.mqtt_host, p_mqtt_host.getValue(), sizeof(config.mqtt_host));
  strlcpy(config.base_topic, p_base_topic.getValue(), sizeof(config.base_topic));
  config.mqtt_port = (uint16_t)atoi(p_mqtt_port.getValue());
  if (config.mqtt_port == 0) config.mqtt_port = 1883;

  build_topics();

  if (should_save_config) {
    save_config();
    should_save_config = false;
    set_status_message("config saved", 2000);
  }

  set_status_message("ip " + ip_to_string(WiFi.localIP()), 3000);
}

bool force_portal_requested() {
  pinMode(force_portal_pin, INPUT_PULLUP);
  delay(10);
  return digitalRead(force_portal_pin) == LOW;
}

void IRAM_ATTR modulator_isr() {
  sim_tick_count++;

  bres_acc += isr_power_pct;
  if (bres_acc >= 100) {
    bres_acc -= 100;
    isr_output_state = 1;
    GPOS = (1 << ssr_sim_pin);
    sim_on_tick_count++;
  } else {
    isr_output_state = 0;
    GPOC = (1 << ssr_sim_pin);
  }
}

void start_modulator() {
  pinMode(ssr_sim_pin, OUTPUT);
  digitalWrite(ssr_sim_pin, LOW);

  timer1_isr_init();
  timer1_attachInterrupt(modulator_isr);
  timer1_enable(timer1_divider, TIM_EDGE, TIM_LOOP);
  timer1_write(timer1_ticks);
}

void setup() {
  Serial.begin(115200);
  delay(50);

  Wire.begin(i2c_sda, i2c_scl);

  if (!display.begin(SSD1306_SWITCHCAPVCC, oled_addr)) {
    Serial.println("SSD1306 allocation failed");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.display();

  pinMode(ssr_sim_pin, OUTPUT);
  digitalWrite(ssr_sim_pin, LOW);
  start_modulator();

  set_status_message("booting", 1500);
  load_config();
  build_topics();
  load_calibration();

  bool force_portal = force_portal_requested();
  if (force_portal) {
    set_status_message("portal btn held", 2000);
  }

  start_portal_and_connect(force_portal);
  setup_time();

  mqtt.setBufferSize(mqtt_buffer_size);

  last_rssi = WiFi.RSSI();
  last_command_ms = millis();
  power_level_changed_ms = millis();
  prior_power_pct = 0;
  update_power_values(0);
  set_status_message("sim ready", 2000);
}

void loop() {
  ensure_wifi();

  unsigned long now = millis();

  if (!mqtt.connected()) {
    mqtt_online_published = false;
    if (now - last_mqtt_attempt_ms >= mqtt_retry_ms) {
      last_mqtt_attempt_ms = now;
      mqtt_connect();
    }
  } else {
    mqtt.loop();
  }

  if (mqtt.connected() && pending_ls_request) {
    pending_ls_request = false;
    publish_filesystem_listing();
  }

  if (pending_calibration_request) {
    float actual_watts = pending_actual_power_watts;
    pending_calibration_request = false;
    handle_calibration_request(actual_watts);
  }

  if (pending_purge_calibration_request) {
    pending_purge_calibration_request = false;
    purge_calibration_file();
  }

  if (mqtt.connected() && !mqtt_online_published) {
    publish_online_status();
  }

  if (mqtt.connected() && (now - last_heartbeat_ms >= heartbeat_interval_ms)) {
    last_heartbeat_ms = now;
    publish_status(false);
  }

  if (mqtt_traffic_active && (now - last_traffic_anim_ms < spinner_active_ms)) {
    if (now - last_display_ms >= display_interval_ms) {
      spinner_frame = (spinner_frame + 1) & 0x07;
    }
  } else {
    mqtt_traffic_active = false;
  }

  if (now - last_display_ms >= display_interval_ms) {
    last_display_ms = now;
    last_rssi = WiFi.RSSI();
    refresh_displayed_power();

    if (portal_active) {
      show_portal_screen();
    } else {
      render_display();
    }
  }
}
