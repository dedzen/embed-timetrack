#include "SyncServer.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>

// WiFi and WebServer ship with the Arduino-ESP32 core -- no lib_deps entry needed.

static WebServer server(80);
static bool server_running = false;
static volatile bool log_cleared_flag = false;

static void handle_log_csv() {
  File f = SD.open("/log.csv", FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "log.csv not found");
    return;
  }
  server.streamFile(f, "text/csv");
  f.close();
}
static void handle_delete_log() {
  if (SD.exists("/log.csv")) {
    SD.remove("/log.csv");
  }

  // Recreate an empty log with just the header, so the file stays valid
  // for the next append/read cycle rather than being missing entirely.
  File f = SD.open("/log.csv", FILE_WRITE);
  if (f) {
    f.println("timestamp,epoch,event,task");
    f.close();
    Serial.println("[Sync] log.csv deleted and reinitialized via HTTP DELETE");
    server.send(200, "text/plain", "log.csv cleared");
  } else {
    Serial.println("[Sync] Failed to recreate log.csv after delete");
    server.send(500, "text/plain", "Failed to recreate log.csv");
  }

  log_cleared_flag = true;
}

static void handle_root() {
  server.send(200, "text/plain", "T-Embed Task Tracker -- GET /log.csv to download the log");
}

bool sync_server_start(const char *ssid, const char *password,
                        char *ip_out, size_t ip_out_len,
                        uint32_t wifi_timeout_ms) {
  WiFi.begin(ssid, password);
  Serial.println("[Sync] Connecting to WiFi...");

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= wifi_timeout_ms) {
      Serial.println("[Sync] WiFi connect timed out");
      WiFi.disconnect(true);
      return false;
    }
    delay(100);
  }

  IPAddress ip = WiFi.localIP();
  snprintf(ip_out, ip_out_len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  Serial.printf("[Sync] Connected, IP: %s\n", ip_out);

  server.on("/", HTTP_GET, handle_root);
  server.on("/log.csv", HTTP_GET, handle_log_csv);
  server.on("/log.csv", HTTP_DELETE, handle_delete_log); // add this line
  server.begin();
  server_running = true;

  Serial.println("[Sync] HTTP server started");
  return true;
}

void sync_server_handle() {
  if (server_running) server.handleClient();
}

void sync_server_stop() {
  if (server_running) {
    server.stop();
    server_running = false;
  }
  WiFi.disconnect(true);
  Serial.println("[Sync] Server stopped, WiFi disconnected");
}
bool sync_server_log_was_cleared() {
  if (log_cleared_flag) {
    log_cleared_flag = false;
    return true;
  }
  return false;
}
