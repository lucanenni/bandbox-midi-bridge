// Waveshare ESP32-S3-Touch-LCD-3.49 (V2) front-end: 640x172 landscape touch UI via LVGL 9.3.
// Logic lives in the BridgeCore library; display/touch drivers are Waveshare's V2 example (Apache-2.0).
#include <BridgeCore.h>
#include <lvgl.h>
#include "user_config.h"
#include "lvgl_port.h"
#include "i2c_bsp.h"
#include "src/lcd_bl_bsp/lcd_bl_pwm_bsp.h"

static volatile int pressed = -1;
static bool debug = false;
static lv_obj_t *lblTop, *lblEvent, *lblInfo, *btn[5], *btnLbl[5];

static void onBtn(lv_event_t *e) { pressed = (int)(intptr_t)lv_event_get_user_data(e); }

class WsUi : public BridgeUi {
 public:
  void begin() override {
    i2c_master_Init();
    lvgl_port_init();              // display, touch, LVGL task
    lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);
    if (!example_lvgl_lock(-1)) return;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lblTop = lv_label_create(scr);
    lv_obj_set_style_text_font(lblTop, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lblTop, lv_color_white(), 0);
    lv_obj_align(lblTop, LV_ALIGN_TOP_LEFT, 8, 4);
    lblEvent = lv_label_create(scr);
    lv_obj_set_style_text_font(lblEvent, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lblEvent, lv_palette_main(LV_PALETTE_CYAN), 0);
    lv_obj_align(lblEvent, LV_ALIGN_TOP_LEFT, 8, 42);
    lblInfo = lv_label_create(scr);
    lv_obj_set_style_text_color(lblInfo, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(lblInfo, LV_ALIGN_TOP_LEFT, 8, 72);
    const int bw = 640 / 5, bh = 70;
    for (int i = 0; i < 5; i++) {
      btn[i] = lv_button_create(scr);
      lv_obj_set_size(btn[i], bw - 8, bh);
      lv_obj_set_pos(btn[i], i * bw + 4, 172 - bh - 4);
      lv_obj_add_event_cb(btn[i], onBtn, LV_EVENT_PRESSED, (void *)(intptr_t)i);
      btnLbl[i] = lv_label_create(btn[i]);
      lv_obj_set_style_text_font(btnLbl[i], &lv_font_montserrat_20, 0);
      lv_obj_center(btnLbl[i]);
    }
    lv_label_set_text(btnLbl[4], "Log");
    example_lvgl_unlock();
  }
  int pollTouch() override { int p = pressed; pressed = -1; if (p == 4) { debug = !debug; return -2; } return p; }
  void draw(const BridgeStatus &s) override {
    if (!example_lvgl_lock(100)) return;
    const char *bs = s.ble == B_CONNECTED ? "AMP OK" : s.ble == B_FOUND ? "connecting" : "scanning";
    lv_label_set_text_fmt(lblTop, "%s  P%02d   BLE %s   USB %s", s.pageName, s.presetNo, bs, s.usbSeen ? "ok" : "-");
    lv_label_set_text(lblEvent, s.lastEvent.c_str());
    if (!debug) lv_label_set_text_fmt(lblInfo, "%s", s.presetName.c_str());
    else lv_label_set_text_fmt(lblInfo, "%s ch%u | %s", s.ampName.c_str(), s.ampChannel, s.logCount ? s.log[s.logCount - 1].c_str() : "-");
    for (int i = 0; i < 4; i++) lv_label_set_text(btnLbl[i], s.labels[i]);
    example_lvgl_unlock();
  }
};

static WsUi ui;
void setup() { bridgeBegin(&ui); }
void loop() { bridgeLoop(); }
