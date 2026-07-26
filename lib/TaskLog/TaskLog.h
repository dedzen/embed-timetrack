
#pragma once
#include <Arduino.h>
#include <time.h>

struct ActiveTaskState {
  bool active;
  char task_name[32];
  time_t start_epoch;
};

bool sd_init();
bool log_task_event(const char *event, const char *task_name); // event = "start" or "end"
ActiveTaskState read_active_task_from_log(); // reconstructs state from the last log.csv line
