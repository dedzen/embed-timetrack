#pragma once
#include <Arduino.h>

// Connects to WiFi and starts an HTTP server serving log.csv from SD.
// On success, fills ip_out with the device's IP (e.g. "192.168.1.42") and returns true.
bool sync_server_start(const char *ssid, const char *password,
                        char *ip_out, size_t ip_out_len,
                        uint32_t wifi_timeout_ms = 5000);

// Call every loop() iteration while the sync screen is active.
void sync_server_handle();

// Stops the server and disconnects WiFi.
void sync_server_stop();
