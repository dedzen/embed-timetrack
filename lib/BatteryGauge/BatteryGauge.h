#pragma once
#include <Arduino.h>

bool battery_gauge_init();
int read_battery_percent(); // 0-100, or -1 if the read failed
