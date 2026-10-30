// M5Stack CoreS3 front-end (320x240 touch). Logic lives in the BridgeCore library.
#include <M5Unified.h>
#include <BridgeCore.h>

class M5Ui : public BridgeUi {
  M5Canvas canvas{&M5.Display};
  bool canvasOk = false, debug = false, settingsView = false;
 public:
  void begin() override {
    auto cfg = M5.config(); M5.begin(cfg);
    M5.Display.setBrightness(120);
  }
  int pollTouch() override {
    M5.update();
    auto t = M5.Touch.getDetail();
    if (!t.wasPressed()) return -1;
    int w = M5.Display.width(), h = M5.Display.height();
    if (settingsView) {  // rows of 48 px from y=44; Back at the bottom
      auto &st = bridgeSettings();
      if (t.y >= h - 56) { settingsView = false; return -2; }
      int row = (t.y - 44) / 48;
      if (row == 0) { st.exprEnabled = !st.exprEnabled; bridgeSaveSettings(); return -2; }
      if (row == 1) { st.exprInvert = !st.exprInvert; bridgeSaveSettings(); return -2; }
      return -1;
    }
    if (t.y < 40 && t.x > w - 50) { settingsView = true; return -2; }
    if (t.y < h - 70) return -1;
    int k = t.x / (w / 5);
    if (k == 4) { debug = !debug; return -2; }
    return k;
  }
  void drawGear(M5Canvas &d, int cx, int cy) {
    for (int i = 0; i < 8; i++) {  // teeth
      float a = i * 0.7854f;
      d.drawLine(cx + cosf(a) * 7, cy + sinf(a) * 7, cx + cosf(a) * 12, cy + sinf(a) * 12, TFT_LIGHTGREY);
      d.drawLine(cx + cosf(a) * 7 + 1, cy + sinf(a) * 7, cx + cosf(a) * 12 + 1, cy + sinf(a) * 12, TFT_LIGHTGREY);
    }
    d.drawCircle(cx, cy, 8, TFT_LIGHTGREY); d.drawCircle(cx, cy, 3, TFT_LIGHTGREY);
  }
  void drawSettings() {
    int w = M5.Display.width(), h = M5.Display.height();
    auto &d = canvas; auto &st = bridgeSettings();
    d.fillScreen(TFT_BLACK); d.setTextSize(2); d.setTextColor(TFT_CYAN); d.setCursor(4, 4); d.print("Settings");
    const char *names[2] = {"Expression pedal", "Expression invert"};
    bool vals[2] = {st.exprEnabled, st.exprInvert};
    for (int i = 0; i < 2; i++) {
      int y = 44 + i * 48;
      d.drawRect(2, y, w - 4, 42, TFT_DARKGREY);
      d.setTextColor(TFT_WHITE); d.setCursor(10, y + 13); d.print(names[i]);
      d.setTextColor(vals[i] ? TFT_GREEN : TFT_RED); d.setCursor(w - 70, y + 13); d.print(vals[i] ? "ON" : "OFF");
    }
    d.setTextSize(1); d.setTextColor(TFT_DARKGREY); d.setCursor(4, 148); d.print("Invert: pedal down = loud");
    d.setTextSize(2); d.setTextColor(TFT_WHITE);
    d.drawRect(2, h - 54, w - 4, 50, TFT_WHITE); d.setCursor(w / 2 - 24, h - 38); d.print("Back");
    M5.Display.startWrite(); canvas.pushSprite(0, 0); M5.Display.endWrite();
  }
  void drawTuner(const BridgeStatus &s) {
    int w = M5.Display.width(), h = M5.Display.height();
    if (!canvasOk) { canvas.setColorDepth(16); canvasOk = canvas.createSprite(w, h) != nullptr; }
    if (!canvasOk) return;
    auto &d = canvas;  // draw off-screen, push once: no flicker
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_CYAN); d.setTextSize(2); d.setCursor(4, 4); d.print("TUNER");
    if (s.tunerHz > 0) {
      float n = 12.0f * log2f(s.tunerHz / 440.0f) + 69.0f;
      int mn = (int)lroundf(n); float cents = (n - mn) * 100.0f;
      static const char *NN[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
      bool ok = fabsf(cents) < 5;
      d.setTextColor(ok ? TFT_GREEN : TFT_WHITE); d.setTextSize(9);
      d.setCursor(w / 2 - 54, 50); d.printf("%s", NN[mn % 12]);
      d.setTextSize(2); d.setTextColor(TFT_LIGHTGREY);
      d.setCursor(4, 140); d.printf("%.1f Hz   %+.0f cents", s.tunerHz, cents);
      int cx = w / 2, cy = 172, bw = (int)(fminf(fabsf(cents), 50) / 50 * (w / 2 - 10));
      d.drawFastVLine(cx, cy - 10, 20, TFT_WHITE);
      d.fillRect(cents < 0 ? cx - bw : cx, cy - 6, bw ? bw : 1, 12, ok ? TFT_GREEN : TFT_ORANGE);
    } else {
      d.setTextColor(TFT_DARKGREY); d.setTextSize(3); d.setCursor(40, 90); d.print("play a string");
    }
    int bw2 = w / 5, by = h - 40;
    d.setTextSize(2); d.setTextColor(TFT_WHITE);
    d.drawRect(2 * bw2 + 2, by, bw2 - 4, 36, TFT_WHITE); d.setCursor(2 * bw2 + 6, by + 10); d.print("Exit");
    M5.Display.startWrite(); canvas.pushSprite(0, 0); M5.Display.endWrite();
  }
  void draw(const BridgeStatus &s) override {
    if (s.tunerOn) { drawTuner(s); return; }
    int w = M5.Display.width(), h = M5.Display.height();
    if (!canvasOk) { canvas.setColorDepth(16); canvasOk = canvas.createSprite(w, h) != nullptr; }
    if (!canvasOk) return;
    if (settingsView) { drawSettings(); return; }
    auto &d = canvas;
    d.fillScreen(TFT_BLACK); d.setTextColor(TFT_WHITE);
    const char *bs = s.ble == B_CONNECTED ? "AMP OK" : s.ble == B_FOUND ? "connecting" : "scanning";
    if (!debug && s.page == 1) {
      d.setTextSize(2); d.setCursor(4, 4);
      d.setTextColor(s.ble == B_CONNECTED ? TFT_GREEN : TFT_YELLOW); d.printf("BLE %s", bs);
      d.setTextColor(s.usbSeen ? TFT_GREEN : TFT_RED); d.printf("  USB %s", s.usbSeen ? "ok" : "-");
      d.setTextColor(TFT_CYAN); d.setCursor(4, 28); d.print(s.pageName);
      String st = s.loopState;
      d.setTextColor(st == "REC" ? TFT_RED : st == "DUB" ? TFT_ORANGE : st == "PLAY" ? TFT_GREEN : TFT_LIGHTGREY);
      d.setTextSize(7); d.setCursor(4, 56); d.print(st);
      int bx = 4, bwid = w - 8, by2 = 124;
      d.drawRect(bx, by2, bwid, 18, TFT_WHITE);
      if (s.loopLenMs) d.fillRect(bx + 2, by2 + 2, (bwid - 4) * (uint64_t)s.loopPosMs / s.loopLenMs, 14, st == "DUB" ? TFT_ORANGE : TFT_GREEN);
      d.setTextSize(2); d.setTextColor(TFT_LIGHTGREY); d.setCursor(4, 148);
      if (s.loopLenMs && s.loopSecs > 0) d.printf("%.1f / %.1f s", s.loopSecs * s.loopPosMs / s.loopLenMs, s.loopSecs);
    } else if (!debug) {
      d.setTextSize(2); d.setCursor(4, 4);
      d.setTextColor(s.ble == B_CONNECTED ? TFT_GREEN : TFT_YELLOW); d.printf("BLE %s", bs);
      d.setTextColor(s.usbSeen ? TFT_GREEN : TFT_RED); d.printf("  USB %s", s.usbSeen ? "ok" : "-");
      d.setTextColor(TFT_CYAN); d.setCursor(4, 28); d.print(s.pageName);
      d.setTextColor(TFT_WHITE); d.setTextSize(8); d.setCursor(4, 52); d.printf("%02d", s.presetNo);
      d.setTextSize(s.presetName.length() > 14 ? 2 : 3); d.setTextColor(TFT_YELLOW);
      d.setCursor(4, 120); d.print(s.presetName.length() ? s.presetName.c_str() : "");
      d.setTextSize(2); d.setTextColor(TFT_LIGHTGREY); d.setCursor(4, 148); if (s.lastEvent != "-") d.print(s.lastEvent);
    } else {
      d.setTextSize(2); d.setCursor(4, 4); d.printf("Page: %s  P%02d", s.pageName, s.presetNo);
      d.setTextColor(s.ble == B_CONNECTED ? TFT_GREEN : TFT_YELLOW); d.setCursor(4, 28); d.printf("BLE %s", bs);
      d.setTextColor(s.usbSeen ? TFT_GREEN : TFT_RED); d.printf("  USB %s", s.usbSeen ? "ok" : "-");
      d.setTextColor(TFT_CYAN); d.setCursor(4, 52); d.print(s.lastEvent);
      d.setTextSize(1); d.setTextColor(TFT_DARKGREY); d.setCursor(4, 76);
      d.printf("%s ch%u  notify: %s", s.ampName.c_str(), s.ampChannel, s.lastNotify.c_str());
      d.setTextColor(TFT_LIGHTGREY);
      for (int i = 0; i < s.logCount; i++) { d.setCursor(4, 92 + i * 9); d.print(s.log[i].c_str()); }
    }
    drawGear(canvas, w - 22, 18);
    int bw = w / 5, by = h - 70;
    d.setTextSize(2);
    for (int i = 0; i < 5; i++) {
      d.drawRect(i * bw + 2, by, bw - 4, 66, TFT_WHITE);
      d.setTextColor(TFT_WHITE);
      String lb = i < 4 ? s.labels[i] : (debug ? "Back" : "Log");
      int nl = lb.indexOf('\n');  // two-line labels, e.g. "Undo\nClr"
      if (nl < 0) { d.setCursor(i * bw + 6, by + 26); d.print(lb); }
      else { d.setCursor(i * bw + 6, by + 14); d.print(lb.substring(0, nl)); d.setCursor(i * bw + 6, by + 38); d.print(lb.substring(nl + 1)); }
    }
    M5.Display.startWrite(); canvas.pushSprite(0, 0); M5.Display.endWrite();
  }
};

static M5Ui ui;
void setup() { bridgeBegin(&ui); }
void loop() { bridgeLoop(); }
