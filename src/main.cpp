#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "RtcClock.h"
#include "TaskLog.h"
#include "SyncServer.h"
#include "BatteryGauge.h"
#include "TaskDefs.h"
#include "wifi_secrets.h"

// ---------------- Pins (confirmed against official README) ----------------
#define ENCODER_INA     4
#define ENCODER_INB     5
#define ENCODER_KEY     0    // SEL = encoder push button
#define BACK_BUTTON_PIN 6    // BOARD_USER_KEY

#define BOARD_PWR_EN 15
#define LONG_PRESS_MS   1500 // hold BACK this long to trigger deep sleep

TFT_eSPI tft = TFT_eSPI();

// ---------------- LVGL display buffer ----------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 20]; // sized for landscape (larger) dimension

// ---------------- Encoder state ----------------
volatile int16_t enc_diff = 0;
int lastA = HIGH;
lv_indev_t *encoder_indev;

// ---------------- Back button state ----------------
bool backHeld = false;
uint32_t backPressStart = 0;
bool backLongTriggered = false;

// ---------------- Screens & groups ----------------
lv_obj_t *scr_main;
lv_group_t *group_main;

lv_obj_t *scr_time;
lv_group_t *group_time;

lv_obj_t *main_active_label;
lv_obj_t *active_task_label;

lv_obj_t *btn_main_time;
lv_obj_t *btn_main_notes;
lv_obj_t *btn_main_settings;
lv_obj_t *scr_settings;
lv_group_t *group_settings;
lv_obj_t *btn_settings_sync;

lv_obj_t *scr_sync;
lv_obj_t *sync_status_label;
bool sync_active = false;
lv_indev_drv_t indev_drv;


lv_obj_t *scr_time_sync;
lv_obj_t *time_sync_status_label;
lv_obj_t *btn_settings_synctime;

lv_obj_t *topbar_time_label;
lv_obj_t *topbar_batt_label;


lv_obj_t *category_buttons[task_category_count];
lv_obj_t *btn_clear;

lv_obj_t *category_popup = nullptr;
lv_group_t *group_popup = nullptr;
int active_category_index = -1;
ActiveTaskState g_active; // reconstructed from log.csv at boot, updated in RAM after that


static lv_style_t style_clear_btn;
bool style_clear_initialized = false;


void update_task_list_visuals() {
  char active_category[32] = "";
  if (g_active.active) {
    const char *sep = strstr(g_active.task_name, ": ");
    if (sep) {
      size_t len = sep - g_active.task_name;
      if (len >= sizeof(active_category)) len = sizeof(active_category) - 1;
      strncpy(active_category, g_active.task_name, len);
      active_category[len] = '\0';
    }
  }

  for (int i = 0; i < task_category_count; i++) {
    lv_obj_t *btn = category_buttons[i];
    if (!btn) continue;
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (!label) continue;

    if (g_active.active && strcmp(task_categories[i].name, active_category) == 0) {
      char buf[40];
      snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", task_categories[i].name);
      lv_label_set_text(label, buf);
    } else {
      lv_label_set_text(label, task_categories[i].name);
    }
  }
}
void format_duration(long total_seconds, char *buf, size_t buf_len) {
  if (total_seconds < 0) total_seconds = 0;
  long h = total_seconds / 3600;
  long m = (total_seconds % 3600) / 60;
  long s = total_seconds % 60;
  if (h > 0)      snprintf(buf, buf_len, "%ldh %ldm", h, m);
  else if (m > 0) snprintf(buf, buf_len, "%ldm %llds", m, (long long)s);
  else            snprintf(buf, buf_len, "%llds", (long long)s);
}
void refresh_active_labels() {
  char buf[64];
  if (g_active.active) {
    long elapsed = (long)(time(nullptr) - g_active.start_epoch);
    char durbuf[16];
    format_duration(elapsed, durbuf, sizeof(durbuf));
    snprintf(buf, sizeof(buf), "Active: %s (%s)", g_active.task_name, durbuf);
  } else {
    snprintf(buf, sizeof(buf), "No active task");
  }
  if (main_active_label) lv_label_set_text(main_active_label, buf);
  if (active_task_label) lv_label_set_text(active_task_label, buf);
}


void create_top_bar(lv_obj_t *parent, lv_obj_t **time_label_out, lv_obj_t **batt_label_out) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, tft.width(), 20);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x202020), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

  lv_obj_t *time_label = lv_label_create(bar);
  lv_label_set_text(time_label, "--:--");
  lv_obj_set_style_text_color(time_label, lv_color_white(), 0); // <-- add this
  lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 5, 0);

  lv_obj_t *batt_label = lv_label_create(bar);
  lv_label_set_text(batt_label, "--%");
  lv_obj_set_style_text_color(batt_label, lv_color_white(), 0); // <-- add this
  lv_obj_align(batt_label, LV_ALIGN_RIGHT_MID, -5, 0);

  *time_label_out = time_label;
  *batt_label_out = batt_label;
}

void update_top_bar() {
  if (!topbar_time_label || !topbar_batt_label) return;

  char timebuf[6];
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  strftime(timebuf, sizeof(timebuf), "%H:%M", &ti);
  lv_label_set_text(topbar_time_label, timebuf);

  int pct = read_battery_percent();
  char battbuf[8];
  if (pct >= 0) snprintf(battbuf, sizeof(battbuf), "%d%%", pct);
  else          snprintf(battbuf, sizeof(battbuf), "?%%");
  lv_label_set_text(topbar_batt_label, battbuf);
}




// ===================================================================
// Deep sleep
// ===================================================================

void enter_deep_sleep() {
  Serial.println("Entering deep sleep...");
  tft.writecommand(0x10);
  digitalWrite(TFT_BL, LOW);

  // Must wait for physical release -- otherwise ext0 wake condition
  // (pin already LOW) is satisfied instantly and we wake right back up.
  while (digitalRead(BACK_BUTTON_PIN) == LOW) {
    delay(10);
  }
  delay(50); // debounce

  esp_sleep_enable_ext0_wakeup((gpio_num_t)BACK_BUTTON_PIN, 0);
  esp_deep_sleep_start();
}
// ===================================================================
// Screen builders
// ===================================================================
void show_main_menu();
void show_time_menu();
void show_settings_menu();
void show_sync_screen();
void show_time_sync_screen();


void settings_menu_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_target(e);
  if (btn == btn_settings_sync) {
    show_sync_screen();
  }else if (btn == btn_settings_synctime) {
    show_time_sync_screen();
  }
}


void close_category_popup() {
  if (category_popup) {
    lv_obj_del(category_popup);
    category_popup = nullptr;
  }
  if (group_popup) {
    lv_group_del(group_popup);
    group_popup = nullptr;
  }
  active_category_index = -1;
  lv_indev_set_group(encoder_indev, group_time);
}

void child_task_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (active_category_index < 0) return;

  int child_idx = (int)(intptr_t)lv_event_get_user_data(e);
  const TaskCategory &cat = task_categories[active_category_index];
  const char *child_name = cat.children[child_idx];

  char full_name[32];
  snprintf(full_name, sizeof(full_name), "%s: %s", cat.name, child_name);

  if (g_active.active) {
    log_task_event("end", g_active.task_name);
  }
  log_task_event("start", full_name);
  g_active.active = true;
  g_active.start_epoch = time(nullptr);
  strncpy(g_active.task_name, full_name, sizeof(g_active.task_name) - 1);
  g_active.task_name[sizeof(g_active.task_name) - 1] = '\0';

  refresh_active_labels();
  update_task_list_visuals();
  close_category_popup();
}

void open_category_popup(int cat_idx) {
  const TaskCategory &cat = task_categories[cat_idx];

  category_popup = lv_obj_create(scr_time);
  lv_obj_set_size(category_popup, tft.width() - 20, tft.height() - 20);
  lv_obj_align(category_popup, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(category_popup, lv_color_hex(0xc9d4a1), 0);
  lv_obj_set_style_bg_opa(category_popup, LV_OPA_COVER, 0.5f);
  lv_obj_set_style_border_width(category_popup, 2, 0);
  lv_obj_set_style_border_color(category_popup, lv_color_black(), 0);

  lv_obj_t *title = lv_label_create(category_popup);
  lv_label_set_text(title, cat.name);
  lv_obj_set_style_text_color(title, lv_color_black(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  lv_obj_update_layout(category_popup);
  Serial.printf("popup size: %d x %d\n", lv_obj_get_width(category_popup), lv_obj_get_height(category_popup));

  lv_obj_t *list = lv_list_create(category_popup);
  lv_obj_set_size(list, lv_obj_get_width(category_popup) - 10, lv_obj_get_height(category_popup) - 40);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);

  group_popup = lv_group_create();
  lv_obj_t *first_btn = nullptr;

  // If a child of THIS category is currently active, tick it in the popup.
  const char *active_child = nullptr;
  char active_category_name[32] = "";
  if (g_active.active) {
    const char *sep = strstr(g_active.task_name, ": ");
    if (sep) {
      size_t len = sep - g_active.task_name;
      if (len >= sizeof(active_category_name)) len = sizeof(active_category_name) - 1;
      strncpy(active_category_name, g_active.task_name, len);
      active_category_name[len] = '\0';
      if (strcmp(active_category_name, cat.name) == 0) {
        active_child = sep + 2;
      }
    }
  }

  for (int i = 0; i < cat.child_count; i++) {
    const char *icon = (active_child && strcmp(active_child, cat.children[i]) == 0) ? LV_SYMBOL_OK : NULL;
    lv_obj_t *btn = lv_list_add_btn(list, icon, cat.children[i]);
    lv_obj_set_style_text_color(btn, lv_color_black(), 0);
    lv_obj_add_event_cb(btn, child_task_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_group_add_obj(group_popup, btn);
    if (!first_btn) first_btn = btn;
    Serial.printf("Added  %s to categoty\n", cat.children[i]);
  }
  Serial.printf("list size: %d x %d, pos: %d,%d\n",
  lv_obj_get_width(list), lv_obj_get_height(list),
  lv_obj_get_x(list), lv_obj_get_y(list));

  active_category_index = cat_idx;
  lv_indev_set_group(encoder_indev, group_popup);
  if (first_btn) lv_group_focus_obj(first_btn);
}

void category_button_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  int cat_idx = (int)(intptr_t)lv_event_get_user_data(e);
  open_category_popup(cat_idx);
}

void clear_button_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_active.active) {
    log_task_event("end", g_active.task_name);
    g_active.active = false;
  }
  refresh_active_labels();
  update_task_list_visuals();
}


void build_time_menu() {
  scr_time = lv_obj_create(NULL);
  group_time = lv_group_create();

  lv_obj_t *title = lv_label_create(scr_time);
  lv_label_set_text(title, "Time - select category");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  active_task_label = lv_label_create(scr_time);
  lv_label_set_text(active_task_label, "No active task");
  lv_obj_align(active_task_label, LV_ALIGN_TOP_MID, 0, 25);

  lv_obj_t *list = lv_list_create(scr_time);
  lv_obj_set_size(list, tft.width() - 10, tft.height() - 60);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);

  btn_clear = lv_list_add_btn(list, NULL, "Clear");
  lv_obj_add_event_cb(btn_clear, clear_button_event_cb, LV_EVENT_CLICKED, NULL);
  lv_group_add_obj(group_time, btn_clear);

  for (int i = 0; i < task_category_count; i++) {
    lv_obj_t *btn = lv_list_add_btn(list, NULL, task_categories[i].name);
    lv_obj_add_event_cb(btn, category_button_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_group_add_obj(group_time, btn);
    category_buttons[i] = btn;
  }


}

void build_time_sync_screen() {
  scr_time_sync = lv_obj_create(NULL);

  lv_obj_t *title = lv_label_create(scr_time_sync);
  lv_label_set_text(title, "Sync Time");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  time_sync_status_label = lv_label_create(scr_time_sync);
  lv_label_set_text(time_sync_status_label, "");
  lv_label_set_long_mode(time_sync_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(time_sync_status_label, tft.width() - 20);
  lv_obj_align(time_sync_status_label, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *hint = lv_label_create(scr_time_sync);
  lv_label_set_text(hint, "Press BACK to return");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void show_time_sync_screen() {
  lv_indev_set_group(encoder_indev, NULL);
  lv_scr_load(scr_time_sync);

  lv_label_set_text(time_sync_status_label, "Connecting to WiFi...");

  bool synced = sync_time_ntp(WIFI_SSID, WIFI_PASSWORD,  5000);

  if (synced) {
    char timebuf[9];
    get_current_time(timebuf, sizeof(timebuf));
    char msg[48];
    snprintf(msg, sizeof(msg), "Synced!\nCurrent time: %s", timebuf);
    lv_label_set_text(time_sync_status_label, msg);
  } else {
    lv_label_set_text(time_sync_status_label, "Sync failed -- check WiFi credentials/signal");
  }
}

void show_time_menu() {
  lv_indev_set_group(encoder_indev, group_time);
  lv_scr_load(scr_time);
  lv_group_focus_obj(btn_clear);
  refresh_active_labels();
  update_task_list_visuals(); // add this line
}

void main_menu_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  lv_obj_t *btn = lv_event_get_target(e);

  if (btn == btn_main_time) {
    show_time_menu();
  } else if (btn == btn_main_notes) {
    Serial.println("Notes selected (not implemented yet)");
  } else if (btn == btn_main_settings) {
    show_settings_menu();
  }
}

void build_main_menu() {
  scr_main = lv_obj_create(NULL);
  group_main = lv_group_create();
  
  create_top_bar(scr_main, &topbar_time_label, &topbar_batt_label);
  
  main_active_label = lv_label_create(scr_main);
  lv_label_set_text(main_active_label, "No active task");
  lv_obj_align(main_active_label, LV_ALIGN_TOP_MID, 0, 25);

  lv_obj_t *list = lv_list_create(scr_main);
  lv_obj_set_size(list, tft.width() - 10, tft.height() - 65);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);

  btn_main_time     = lv_list_add_btn(list, LV_SYMBOL_HOME, "Time");
  btn_main_notes    = lv_list_add_btn(list, LV_SYMBOL_SAVE, "Notes");
  btn_main_settings = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "Settings");

  lv_obj_add_event_cb(btn_main_time,     main_menu_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(btn_main_notes,    main_menu_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(btn_main_settings, main_menu_event_cb, LV_EVENT_CLICKED, NULL);

  lv_group_add_obj(group_main, btn_main_time);
  lv_group_add_obj(group_main, btn_main_notes);
  lv_group_add_obj(group_main, btn_main_settings);
}

void show_main_menu() {
  lv_indev_set_group(encoder_indev, group_main);
  lv_scr_load(scr_main);
  lv_group_focus_obj(btn_main_time);
}




void build_settings_menu() {
  scr_settings = lv_obj_create(NULL);
  group_settings = lv_group_create();

  lv_obj_t *title = lv_label_create(scr_settings);
  lv_label_set_text(title, "Settings");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  lv_obj_t *list = lv_list_create(scr_settings);
  lv_obj_set_size(list, tft.width() - 10, tft.height() - 40);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);

  btn_settings_sync = lv_list_add_btn(list, LV_SYMBOL_WIFI, "Sync with PC");
  lv_obj_add_event_cb(btn_settings_sync, settings_menu_event_cb, LV_EVENT_CLICKED, NULL);
  lv_group_add_obj(group_settings, btn_settings_sync);

  btn_settings_synctime = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Sync Time");
  lv_obj_add_event_cb(btn_settings_synctime, settings_menu_event_cb, LV_EVENT_CLICKED, NULL);
  lv_group_add_obj(group_settings, btn_settings_synctime);
}

void show_settings_menu() {
  lv_indev_set_group(encoder_indev, group_settings);
  lv_scr_load(scr_settings);
  lv_group_focus_obj(btn_settings_sync);
}
void build_sync_screen() {
  scr_sync = lv_obj_create(NULL);

  lv_obj_t *title = lv_label_create(scr_sync);
  lv_label_set_text(title, "Sync with PC");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  sync_status_label = lv_label_create(scr_sync);
  lv_label_set_text(sync_status_label, "");
  lv_label_set_long_mode(sync_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sync_status_label, tft.width() - 20);
  lv_obj_align(sync_status_label, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t *hint = lv_label_create(scr_sync);
  lv_label_set_text(hint, "Press BACK to stop");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void show_sync_screen() {
  lv_indev_set_group(encoder_indev, NULL); // nothing selectable on this screen
  lv_scr_load(scr_sync);

  lv_label_set_text(sync_status_label, "Connecting to WiFi...");
  //_lv_disp_refr_timer(display->refr_timer); // force redraw now -- WiFi.begin() blocks below

  char ip_buf[16];
  bool ok = sync_server_start(WIFI_SSID, WIFI_PASSWORD, ip_buf, sizeof(ip_buf), 5000);

  if (ok) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Serving at:\nhttp://%s/log.csv", ip_buf);
    lv_label_set_text(sync_status_label, msg);
    sync_active = true;
  } else {
    lv_label_set_text(sync_status_label, "WiFi connection failed");
    sync_active = false;
  }
}
// ===================================================================
// BACK navigation
// ===================================================================
void go_back() {
  if (category_popup) {
    close_category_popup();
    return;
  }
  if (lv_scr_act()==scr_main){
    enter_deep_sleep();
  }
  else if (lv_scr_act() == scr_time) {
    show_main_menu();
  } else if (lv_scr_act() == scr_settings) {
    show_main_menu();
  } else if (lv_scr_act() == scr_sync) {
    if (sync_active) { sync_server_stop(); sync_active = false; }
    show_settings_menu();
  } else if (lv_scr_act() == scr_time_sync) {
    show_settings_menu();
  }
}
// ===================================================================
// LVGL display flush
// ===================================================================


void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {

  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}
// ===================================================================
// Encoder
// ===================================================================
void IRAM_ATTR readEncoder() {
  int a = digitalRead(ENCODER_INA);
  int b = digitalRead(ENCODER_INB);
  if (a != lastA) {
    enc_diff += (b != a) ? 1 : -1;
    lastA = a;
  }
}


void encoder_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  data->enc_diff = enc_diff;
  data->state = (digitalRead(ENCODER_KEY) == LOW) ? LV_INDEV_STATE_PRESSED
                                                    : LV_INDEV_STATE_RELEASED;
  enc_diff = 0;
}
//===================================================================
// BACK button: short press = back, 3s hold = deep sleep
// ===================================================================
void handle_back_button() {
  bool pressed = (digitalRead(BACK_BUTTON_PIN) == LOW);

  if (pressed && !backHeld) {
    backHeld = true;
    backLongTriggered = false;
    backPressStart = millis();
  } else if (pressed && backHeld) {
    if (!backLongTriggered && (millis() - backPressStart >= LONG_PRESS_MS)) {
      backLongTriggered = true;
      enter_deep_sleep();
    }
  } else if (!pressed && backHeld) {
    if (!backLongTriggered) {
      go_back();
    }
    backHeld = false;
  }
}

// ===================================================================
// Setup / loop
// ===================================================================

void setup() {
  Serial.begin(115200);

  pinMode(BOARD_PWR_EN, OUTPUT);
  digitalWrite(BOARD_PWR_EN, HIGH);  // enable peripheral power rail
  delay(50);       
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  apply_timezone();

  pinMode(ENCODER_INA, INPUT_PULLUP);
  pinMode(ENCODER_INB, INPUT_PULLUP);
  pinMode(ENCODER_KEY, INPUT_PULLUP);
  pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);

  lastA = digitalRead(ENCODER_INA);
  attachInterrupt(digitalPinToInterrupt(ENCODER_INA), readEncoder, CHANGE);
  battery_gauge_init();

  tft.init();
  tft.setRotation(3); // must come before reading tft.width()/height()

  //char dbgbuf[24];
  //get_full_datetime(dbgbuf, sizeof(dbgbuf));
  //Serial.printf("[Boot] RTC time at startup: %s\n", dbgbuf);  

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, tft.width() * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = tft.width();
  disp_drv.ver_res = tft.height();
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  //lv_log_register_print_cb(my_log_cb);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_ENCODER;
  indev_drv.read_cb = encoder_read;
  encoder_indev = lv_indev_drv_register(&indev_drv);

  if (sd_init()) {
    g_active = read_active_task_from_log();
    if (g_active.active) {
      Serial.printf("[Boot] Resumed active task: %s\n", g_active.task_name);
    }
  } else {
    g_active.active = false;
  }

  build_main_menu();
  build_time_menu();
  build_settings_menu();
  build_sync_screen();
  build_time_sync_screen();
  update_task_list_visuals(); // add this line

  show_main_menu();
}

void loop() {
  static uint32_t last_tick = 0;
  uint32_t now = millis();
  lv_tick_inc(now - last_tick);
  last_tick = now;

  if (sync_active) {
    sync_server_handle();
    if (sync_server_log_was_cleared()) {
      g_active.active = false;
      refresh_active_labels();
      update_task_list_visuals();
      Serial.println("[Sync] Log cleared remotely -- active task state reset");
    }
  }

  static uint32_t last_refresh = 0;
  if (millis() - last_refresh > 1000) {
    last_refresh = millis();
    if (g_active.active) refresh_active_labels();
    if (lv_scr_act() == scr_main) update_top_bar();
  }

  lv_timer_handler();
  handle_back_button();   
  delay(5);

}
