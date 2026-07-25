#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 20];   // sized for the larger landscape dimension

void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);   // do this BEFORE reading width()/height()

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, tft.width() * 20);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = tft.width();   // now correctly 320 post-rotation
  disp_drv.ver_res = tft.height();  // now correctly 170 post-rotation
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_obj_t *label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, "LVGL is alive");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  Serial.println("LVGL init complete.");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
