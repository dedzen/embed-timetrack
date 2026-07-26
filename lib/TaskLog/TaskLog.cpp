#include "TaskLog.h"
#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h> // for TFT_eSPI::getSPIinstance()

// SD shares the TFT's physical SPI bus (per LilyGO's own tf_card_test example) -- same
// SCK/MISO/MOSI, separate CS. Do not run SD and TFT transactions concurrently from an ISR.

#define SD_CS   13
// SD_SCK/SD_MISO/SD_MOSI no longer needed here -- TFT_eSPI already
// configured this exact bus during tft.init(), we just reuse it.

static const char *LOG_PATH = "/log.csv";

extern TFT_eSPI tft; // defined in main.cpp

bool sd_init() {
  // Share TFT_eSPI's existing SPI bus instead of starting a second,
  // conflicting one on the same physical pins.
  SPIClass &sharedSPI = tft.getSPIinstance();

  if (!SD.begin(SD_CS, sharedSPI)) {
    Serial.println("[SD] Mount failed -- check card is inserted and FAT32 formatted");
    return false;
  }
  Serial.println("[SD] Mounted OK");

  if (!SD.exists(LOG_PATH)) {
    File f = SD.open(LOG_PATH, FILE_WRITE);
    if (f) {
      f.println("timestamp,epoch,event,task");
      f.close();
    } else {
      Serial.println("[SD] Failed to create log.csv");
    }
  }
  return true;
}
bool log_task_event(const char *event, const char *task_name) {
  File f = SD.open(LOG_PATH, FILE_APPEND); // "a" -- append only, never truncates
  if (!f) {
    Serial.println("[SD] Failed to open log.csv for append");
    return false;
  }

  time_t now = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char timebuf[20];
  strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &timeinfo);

  f.printf("%s,%ld,%s,%s\n", timebuf, (long)now, event, task_name);
  f.close();

  Serial.printf("[SD] Logged: %s,%ld,%s,%s\n", timebuf, (long)now, event, task_name);
  return true;
}

ActiveTaskState read_active_task_from_log() {
  ActiveTaskState state = {};
  state.active = false;

  File f = SD.open(LOG_PATH, FILE_READ);
  if (!f) {
    Serial.println("[SD] Could not open log.csv for reading");
    return state;
  }

  String lastLine = "";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0 && line != "timestamp,epoch,event,task") {
      lastLine = line;
    }
  }
  f.close();

  if (lastLine.length() == 0) return state; // empty log -- nothing active

  int c1 = lastLine.indexOf(',');
  int c2 = lastLine.indexOf(',', c1 + 1);
  int c3 = lastLine.indexOf(',', c2 + 1);
  if (c1 < 0 || c2 < 0 || c3 < 0) {
    Serial.println("[SD] Malformed last log line, treating as no active task");
    return state;
  }

  String epochStr = lastLine.substring(c1 + 1, c2);
  String eventStr = lastLine.substring(c2 + 1, c3);
  String taskStr  = lastLine.substring(c3 + 1);

  if (eventStr == "start") {
    state.active = true;
    state.start_epoch = (time_t)epochStr.toInt();
    taskStr.toCharArray(state.task_name, sizeof(state.task_name));
  }
  // if last event was "end" (or anything else), state.active stays false

  return state;
}
