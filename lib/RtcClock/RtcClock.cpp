#include "RtcClock.h"
#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include "esp_sntp.h"

static volatile bool ntp_sync_completed = false;

static void time_sync_notification_cb(struct timeval *tv) {
  ntp_sync_completed = true;
}

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
void get_full_datetime(char *buf, size_t buf_len) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(buf, buf_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void apply_timezone() {
  setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
  tzset();
}


bool sync_time_ntp(const char *ssid, const char *password, uint32_t timeout_ms) {
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

  // ntp_sync_completed = false;
  // sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  // configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org");
  ntp_sync_completed = false;
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  sntp_setservername(0, "pool.ntp.org");
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  if (!sntp_enabled()) sntp_init();

  uint32_t elapsed = millis() - start;
  uint32_t remaining = (elapsed < timeout_ms) ? (timeout_ms - elapsed) : 500;
  uint32_t wait_start = millis();

  // Wait for the REAL callback, not a heuristic guess based on the clock's current value.
  while (!ntp_sync_completed && (millis() - wait_start < remaining)) {
    delay(50);
  }

  WiFi.disconnect(true);

  if (ntp_sync_completed) {
    Serial.println("[NTP] Time synced successfully (confirmed via callback)");
  } else {
    Serial.println("[NTP] Sync failed or timed out (no callback fired)");
  }

  return ntp_sync_completed;
}
