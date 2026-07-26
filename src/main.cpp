#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "RtcClock.h"
#include "wifi_secrets.h"

// ---------------- Pins (confirmed against official README) ----------------
#define ENCODER_INA     4
#define ENCODER_INB     5
#define ENCODER_KEY     0    // SEL = encoder push button
#define BACK_BUTTON_PIN 6    // BOARD_USER_KEY

#define BOARD_PWR_EN 15
#define LONG_PRESS_MS   3000 // hold BACK this long to trigger deep sleep

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

lv_obj_t *active_task_label;

lv_obj_t *btn_main_time;
lv_obj_t *btn_main_notes;
lv_obj_t *btn_main_settings;
lv_indev_drv_t indev_drv;

const char *task_names[] = { "Sleep", "Sport", "Transport" };
const int task_count = sizeof(task_names) / sizeof(task_names[0]);
lv_obj_t *task_buttons[10];

int active_task_index = -1;

// ===================================================================
// Mock SD write -- replace with real SD card logic later.
// ===================================================================
void write_task_to_sd_mock(const char *task_name) {
  char timebuf[9];
  get_current_time(timebuf, sizeof(timebuf));
  Serial.printf("[SD MOCK] %s -- active task: %s\n", timebuf, task_name);}

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

void time_task_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  lv_obj_t *btn = lv_event_get_target(e);

  for (int i = 0; i < task_count; i++) {
    if (task_buttons[i] == btn) {
      active_task_index = i;
      write_task_to_sd_mock(task_names[i]);
//      lv_label_set_text_fmt(active_task_label, "Active: %s", task_names[i]);
      show_main_menu();   
      break;
    }
  }
}

void build_time_menu() {
  scr_time = lv_obj_create(NULL);
  group_time = lv_group_create();

  lv_obj_t *title = lv_label_create(scr_time);
  lv_label_set_text(title, "Time - select task");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  active_task_label = lv_label_create(scr_time);
  lv_label_set_text(active_task_label, "Active: none");
  lv_obj_align(active_task_label, LV_ALIGN_TOP_MID, 0, 25);

  lv_obj_t *list = lv_list_create(scr_time);
  lv_obj_set_size(list, tft.width() - 10, tft.height() - 60);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);

  for (int i = 0; i < task_count; i++) {
    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_OK, task_names[i]);
    lv_obj_add_event_cb(btn, time_task_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(group_time, btn);
    task_buttons[i] = btn;
  }
}

void show_time_menu() {
  lv_indev_set_group(encoder_indev, group_time);
  lv_scr_load(scr_time);
  lv_group_focus_obj(task_buttons[0]);
}

void main_menu_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  lv_obj_t *btn = lv_event_get_target(e);

  if (btn == btn_main_time) {
    show_time_menu();
  } else if (btn == btn_main_notes) {
    Serial.println("Notes selected (not implemented yet)");
  } else if (btn == btn_main_settings) {
    Serial.println("Settings selected (not implemented yet)");
  }
}

void build_main_menu() {
  scr_main = lv_obj_create(NULL);
  group_main = lv_group_create();

  lv_obj_t *title = lv_label_create(scr_main);
  lv_label_set_text(title, "Main Menu");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

  lv_obj_t *list = lv_list_create(scr_main);
  lv_obj_set_size(list, tft.width() - 10, tft.height() - 40);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);

  btn_main_time     = lv_list_add_btn(list, LV_SYMBOL_OK, "Time");
  btn_main_notes    = lv_list_add_btn(list, LV_SYMBOL_OK, "Notes");
  btn_main_settings = lv_list_add_btn(list, LV_SYMBOL_OK, "Settings");

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

// ===================================================================
// BACK navigation
// ===================================================================
void go_back() {
  if (lv_scr_act() == scr_time) {
    show_main_menu();
  }
}

// ===================================================================
// LVGL display flush
// ===================================================================

uint32_t flush_call_count = 0;

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

  pinMode(ENCODER_INA, INPUT_PULLUP);
  pinMode(ENCODER_INB, INPUT_PULLUP);
  pinMode(ENCODER_KEY, INPUT_PULLUP);
  pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);

  lastA = digitalRead(ENCODER_INA);
  attachInterrupt(digitalPinToInterrupt(ENCODER_INA), readEncoder, CHANGE);

  tft.init();
  tft.setRotation(3); // must come before reading tft.width()/height()

  
  bool synced = sync_time_ntp(WIFI_SSID, WIFI_PASSWORD, 2 * 3600, 0, 5000);
  if (!synced) {
    // Fallback: at least get something roughly sane rather than 1970
    set_system_time(2026, 7, 26, 12, 0, 0);
  }

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

  build_main_menu();
  build_time_menu();

  show_main_menu();
}

void loop() {
  static uint32_t last_tick = 0;
  uint32_t now = millis();
  lv_tick_inc(now - last_tick);
  last_tick = now;

  lv_timer_handler();
  handle_back_button();   
  delay(5);

}
