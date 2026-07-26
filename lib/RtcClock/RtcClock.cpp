#include "RtcClock.h"
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>

void set_system_time(int year, int month, int day, int hour, int min, int sec) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon  = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min  = min;
  t.tm_sec  = sec;

  time_t epoch = mktime(&t);
  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  settimeofday(&tv, NULL);
}

void get_current_time(char *buf, size_t buf_len) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(buf, buf_len, "%H:%M:%S", &timeinfo);
}

bool sync_time_ntp(const char *ssid, const char *password,
                    long gmt_offset_sec, int daylight_offset_sec,
                    uint32_t timeout_ms) {
  uint32_t start = millis();

  WiFi.begin(ssid, password);
  Serial.println("[NTP] Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= timeout_ms) {
      Serial.println("[NTP] WiFi connect timed out");
      WiFi.disconnect(true);
      return false;
    }
    delay(100);
  }

  Serial.println("[NTP] WiFi connected, requesting time...");
  configTime(gmt_offset_sec, daylight_offset_sec, "pool.ntp.org");

  // Whatever's left of the original budget goes to the NTP fetch itself.
  uint32_t elapsed = millis() - start;
  uint32_t remaining = (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 500;

  struct tm timeinfo;
  bool ok = getLocalTime(&timeinfo, remaining);

  WiFi.disconnect(true);

  if (ok) {
    Serial.println("[NTP] Time synced successfully");
  } else {
    Serial.println("[NTP] Sync failed or timed out");
  }

  return ok;
}
