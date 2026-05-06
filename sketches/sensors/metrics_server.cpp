// metrics_server.cpp
#include "metrics_server.h"
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "util.h"

String prometheusEscaped(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (uint16_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (c == '\\' || c == '"') out += '\\';
    if (c == '\n' || c == '\r') out += ' ';
    else out += c;
  }
  return out;
}

String prometheusLabelsForSensor(uint8_t i) {
  String labels = "id=\"" + prometheusEscaped(safeDeviceId()) + "\"";
  labels += ",index=\"" + String(i + 1) + "\"";
  labels += ",address=\"" + prometheusEscaped(sensorAddressString(i)) + "\"";
  labels += ",name=\"" + prometheusEscaped(String(sensorNames[i])) + "\"";
  labels += ",topic=\"" + prometheusEscaped(sanitizeTopicPart(String(sensorNames[i]))) + "\"";
  return labels;
}

String buildPrometheusMetrics() {
  String idLabel = prometheusEscaped(safeDeviceId());
  metricsScrapeCount++;
  String m;
  m.reserve(8192);
  m += "# HELP temp_node_online Node online state.\n";
  m += "# TYPE temp_node_online gauge\n";
  m += "temp_node_online{id=\"" + idLabel + "\"} 1\n";
  m += "# HELP temp_wifi_connected WiFi connected state.\n";
  m += "# TYPE temp_wifi_connected gauge\n";
  m += String("temp_wifi_connected{id=\"") + idLabel + "\"} " + (WiFi.status() == WL_CONNECTED ? "1\n" : "0\n");
  m += "# HELP temp_mqtt_connected MQTT connected state.\n";
  m += "# TYPE temp_mqtt_connected gauge\n";
  m += String("temp_mqtt_connected{id=\"") + idLabel + "\"} " + (mqtt.connected() ? "1\n" : "0\n");
  m += "# HELP temp_wifi_rssi_dbm WiFi RSSI in dBm.\n";
  m += "# TYPE temp_wifi_rssi_dbm gauge\n";
  m += "temp_wifi_rssi_dbm{id=\"" + idLabel + "\"} " + String(WiFi.RSSI()) + "\n";
  m += "# HELP temp_free_heap_bytes Free heap bytes.\n";
  m += "# TYPE temp_free_heap_bytes gauge\n";
  m += "temp_free_heap_bytes{id=\"" + idLabel + "\"} " + String(ESP.getFreeHeap()) + "\n";
  m += "# HELP temp_uptime_seconds Uptime in seconds.\n";
  m += "# TYPE temp_uptime_seconds counter\n";
  m += "temp_uptime_seconds{id=\"" + idLabel + "\"} " + String((millis() - bootMillis) / 1000UL) + "\n";
  m += "# HELP temp_sensor_count Number of active sensors.\n";
  m += "# TYPE temp_sensor_count gauge\n";
  m += "temp_sensor_count{id=\"" + idLabel + "\"} " + String(sensorCount) + "\n";
  m += "# HELP temp_sensor_network_detected Real sensor network has ever been detected.\n";
  m += "# TYPE temp_sensor_network_detected gauge\n";
  m += String("temp_sensor_network_detected{id=\"") + idLabel + "\"} " + (sensorNetworkDetected ? "1\n" : "0\n");
  m += "# HELP temp_sensor_simulated Simulated sensors active.\n";
  m += "# TYPE temp_sensor_simulated gauge\n";
  m += String("temp_sensor_simulated{id=\"") + idLabel + "\"} " + (useFakeSensors ? "1\n" : "0\n");
  m += "# HELP temp_prometheus_port Prometheus listener port.\n";
  m += "# TYPE temp_prometheus_port gauge\n";
  m += "temp_prometheus_port{id=\"" + idLabel + "\"} " + String(config.prometheusPort) + "\n";
  m += "# HELP temp_last_sensor_sample_seconds Seconds since last sensor sample.\n";
  m += "# TYPE temp_last_sensor_sample_seconds gauge\n";
  m += "temp_last_sensor_sample_seconds{id=\"" + idLabel + "\"} " + String((millis() - lastSensorSampleMs) / 1000UL) + "\n";
  m += "# HELP temp_mqtt_publish_total Total successful MQTT publishes.\n";
  m += "# TYPE temp_mqtt_publish_total counter\n";
  m += "temp_mqtt_publish_total{id=\"" + idLabel + "\"} " + String(mqttPublishCount) + "\n";
  m += "# HELP temp_prometheus_scrape_total Total Prometheus scrapes served.\n";
  m += "# TYPE temp_prometheus_scrape_total counter\n";
  m += "temp_prometheus_scrape_total{id=\"" + idLabel + "\"} " + String(metricsScrapeCount) + "\n";
  String waterLevelLabelEsc = prometheusEscaped(String(waterLevelLabel(waterLevelIndex)));
  String waterBaseLabels = "id=\"" + idLabel + "\",level=\"" + waterLevelLabelEsc + "\"";
  m += "# HELP water_probe_present Water probe detected during measurement.\n";
  m += "# TYPE water_probe_present gauge\n";
  m += "water_probe_present{" + waterBaseLabels + "} " + String(waterProbePresent ? 1 : 0) + "\n";
  m += "# HELP water_valid Water level reading is valid.\n";
  m += "# TYPE water_valid gauge\n";
  m += "water_valid{" + waterBaseLabels + "} " + String(waterValid ? 1 : 0) + "\n";
  m += "# HELP water_adc_raw Raw ADC reading for water probe.\n";
  m += "# TYPE water_adc_raw gauge\n";
  m += "water_adc_raw{" + waterBaseLabels + "} " + String(waterAdcRaw) + "\n";
  m += "# HELP water_level_index Water level index: 0=no_probe,1=>40gal,2=15-40gal,3=5-15gal,4=<5gal.\n";
  m += "# TYPE water_level_index gauge\n";
  m += "water_level_index{" + waterBaseLabels + "} " + String(int(waterLevelIndex)) + "\n";
  m += "# HELP water_heartbeat_interval_ms Configured water measurement interval in ms.\n";
  m += "# TYPE water_heartbeat_interval_ms gauge\n";
  m += "water_heartbeat_interval_ms{" + waterBaseLabels + "} " + String(config.waterHeartbeatIntervalMs) + "\n";
  m += "# HELP water_measure_window_ms Water probe on-time per measurement in ms.\n";
  m += "# TYPE water_measure_window_ms gauge\n";
  m += "water_measure_window_ms{" + waterBaseLabels + "} " + String(watermeasurewindowms) + "\n";
  m += "# HELP water_last_sample_seconds Seconds since last water sample.\n";
  m += "# TYPE water_last_sample_seconds gauge\n";
  m += "water_last_sample_seconds{" + waterBaseLabels + "} " + String(lastWaterSampleMs > 0 ? ((millis() - lastWaterSampleMs) / 1000UL) : 0) + "\n";
  m += "# HELP water_threshold_adc Water threshold ADC values by level index.\n";
  m += "# TYPE water_threshold_adc gauge\n";
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    m += "water_threshold_adc{id=\"" + idLabel + "\",level=\"" + prometheusEscaped(String(waterLevelLabel(i))) + "\",level_index=\"" + String(i) + "\",label=\"" + prometheusEscaped(String(waterLevelLabel(i))) + "\"} " + String(config.waterThresholds[i]) + "\n";
  }
  for (uint8_t i = 0; i < sensorCount; i++) {
    String labels = prometheusLabelsForSensor(i);
    m += "# HELP temp_sensor_connected Sensor connected state.\n";
    m += "# TYPE temp_sensor_connected gauge\n";
    m += "temp_sensor_connected{" + labels + "} " + String(sensorPresent[i] ? 1 : 0) + "\n";
    if (!isnan(sensorTempsC[i])) {
      m += "# HELP temp_sensor_temp_c Sensor temperature in Celsius.\n";
      m += "# TYPE temp_sensor_temp_c gauge\n";
      m += "temp_sensor_temp_c{" + labels + "} " + String(sensorTempsC[i], 4) + "\n";
      m += "# HELP temp_sensor_temp_f Sensor temperature in Fahrenheit.\n";
      m += "# TYPE temp_sensor_temp_f gauge\n";
      m += "temp_sensor_temp_f{" + labels + "} " + String(sensorTempsC[i] * 9.0f / 5.0f + 32.0f, 4) + "\n";
    }
  }
  return m;
}

void startMetricsServer() {
  if (metricsServer) {
    delete metricsServer;
    metricsServer = nullptr;
  }

  metricsServer = new ESP8266WebServer(config.prometheusPort);

  metricsServer->on("/metrics", HTTP_GET, []() {
    metricsServer->send(200, "text/plain; charset=utf-8", buildPrometheusMetrics());
  });

  metricsServer->on("/", HTTP_GET, []() {
    metricsServer->send(200, "text/plain", "Prometheus metrics available at /metrics");
  });

  metricsServer->begin();
}

void serviceMetricsServer() {
  if (metricsServer) metricsServer->handleClient();
}
