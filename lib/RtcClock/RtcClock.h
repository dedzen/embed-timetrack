#pragma once
#include <Arduino.h>

void set_system_time(int year, int month, int day, int hour, int min, int sec);
void get_current_time(char *buf, size_t buf_len);
void get_full_datetime(char *buf, size_t buf_len);
void apply_timezone();
// Simplified signature -- gmt_offset/daylight_offset removed since we now use
// a proper POSIX TZ string internally, which handles DST transitions correctly.
bool sync_time_ntp(const char *ssid, const char *password, uint32_t timeout_ms = 5000);
