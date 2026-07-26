#pragma once
#include <Arduino.h>

// Sets the ESP32's internal RTC directly (no network needed).
void set_system_time(int year, int month, int day, int hour, int min, int sec);

// Writes the current time as "HH:MM:SS" into buf (buf_len should be >= 9).
void get_current_time(char *buf, size_t buf_len);

// Connects to WiFi and syncs via NTP, aborting after timeout_ms total
// (covers both WiFi connect AND the NTP fetch combined).
// Returns true on success, false on timeout/failure. Always leaves WiFi
// disconnected afterward, regardless of outcome.
bool sync_time_ntp(const char *ssid, const char *password,
                    long gmt_offset_sec = 0, int daylight_offset_sec = 0,
                    uint32_t timeout_ms = 5000);
