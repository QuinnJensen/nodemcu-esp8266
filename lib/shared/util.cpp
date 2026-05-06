#include "util.h"
#include <ctype.h>
#include "app_config.h"
#include "app_state.h"
#include "pins_and_constants.h"

String sanitizeTopicPart(const String& in) {
  String out;
  out.reserve(in.length());
  for (uint16_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    if (isalnum(static_cast<unsigned char>(c))) out += (char)tolower(static_cast<unsigned char>(c));
    else if (c == '-' || c == '_' || c == '.') out += c;
    else if (c == ' ') out += '_';
  }
  if (!out.length()) out = "sensor";
  return out;
}

String safeDeviceId() {
  String id = String(config.deviceId);
  id.trim();
  if (!id.length()) id = "newKid";
  return id;
}

String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

String timeToString(time_t t) {
  if (t < 100000) return "";
  struct tm tmStruct;
  localtime_r(&t, &tmStruct);
  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tmStruct);
  String ts(buf);
  if (ts.length() >= 24) ts = ts.substring(0, 22) + ":" + ts.substring(22);
  return ts;
}

String currentTimestampString() {
  time_t now = time(nullptr);
  return timeToString(now);
}

void setupTimeHelpers() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", devicetz, 1);
  tzset();
  timeConfigured = true;
}
