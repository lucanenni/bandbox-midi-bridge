#include "BridgeCore.h"
#include <USB.h>
#include <USBMIDI.h>
#include <NimBLEDevice.h>
#include <stdarg.h>
#include "bandbox.h"
#include "config.h"

static const char *SERVICE_UUID = "65786365-6c70-6f69-6e74-2e636f6d0000";
static const char *WRITE_UUID   = "65786365-6c70-6f69-6e74-2e636f6d0002";
static const char *NOTIFY_UUID  = "65786365-6c70-6f69-6e74-2e636f6d0001";

static USBMIDI MIDI("Chocolate+ Bridge");
static BridgeUi *ui = nullptr;

// ---- state -------------------------------------------------------------------------------
static int page = 0;
static int presetNo = 1;
static bool tunerOn = false, loopRecording = false, loopHasTake = false, loopPlaying = false, boostOn = false;
static uint32_t lastKeyMs[4], lastUndoMs = 0, loopActMs = 0, loopPollMs = 0;
static uint8_t loopPollStep = 0;
static bool loopQuiet = false;
// The amp's 0x3143/0x3146 values are not time (500 for a ~12 s loop, 3214 for ~9.6 s) and read replies arrive in bursts.
// The loop phase is kept locally: period = time between the amp's wrap notifications (0x3146 = 0, pushed at each wrap).
static uint32_t loopPeriodMs = 0, loopWrapAt = 0, loopRecAt = 0, loopFrozen = 0;
static String lastEvent = "-", lastNotify = "-", ampName = "-";
static uint8_t ampChannel = CHANNEL_TRIO;
static volatile bool dirty = true;
static bool usbSeen = false;
static volatile bool hbPending = false;
// preset inventory (feature 0x3440): repeated [id, genre, name length, UTF-8 name], may span several packets
static const int MAX_PRESETS = 48;
static uint8_t invBuf[1600]; static size_t invLen = 0;
static uint8_t presetIds[MAX_PRESETS]; static String presetNames[MAX_PRESETS]; static int presetCount = 0;
static volatile bool needInit = false;
static float tunerHz = 0; static int tunerString = 0; static uint32_t tunerMs = 0;

static volatile BleState bleState = B_SCAN;
static NimBLEAddress foundAddr;
static NimBLEClient *client = nullptr;
static NimBLERemoteCharacteristic *wChr = nullptr;

// ---- diagnostics log (shown on screen; there is no serial port in USB-MIDI mode) ------------
static String logLines[8]; static int logN = 0;
static void dlog(const char *fmt, ...) {
  char b[80]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  if (logN == 8) { for (int i = 1; i < 8; i++) logLines[i - 1] = logLines[i]; logN = 7; }
  logLines[logN++] = b; dirty = true; Serial.println(b);
}

// ---- BLE ---------------------------------------------------------------------------------
class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    if (bleState != B_SCAN) return;
    std::string n = d->getName();
    if (n.find(AMP_NAME_MATCH) == std::string::npos) return;
    dlog("found %s %s", n.c_str(), d->getAddress().toString().c_str());
    ampName = n.c_str();
    ampChannel = (n.find("Solo") != std::string::npos) ? CHANNEL_SOLO : CHANNEL_TRIO;
    foundAddr = d->getAddress();
    bleState = B_FOUND;
    NimBLEDevice::getScan()->stop();
  }
} scanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    dlog("disconnected r=%d", reason);
    wChr = nullptr; bleState = B_SCAN; dirty = true;
  }
} clientCb;

static void notifyCb(NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
  uint16_t cmd = n >= 4 ? d[2] | (d[3] << 8) : 0;
  if (cmd == 4) return;  // keepalive: not logged
  // read replies (cmd 1) may be fragmented: header = cmd:2 count index len:2, payload = status:2 feature:2 vlen:2 value
  if (cmd == 1 && n >= 8) {
    uint8_t count = d[4], idx = d[5];
    if (idx == 0) invLen = 0;
    if (invLen + (n - 8) <= sizeof invBuf) { memcpy(invBuf + invLen, d + 8, n - 8); invLen += n - 8; }
    if (idx + 1 >= count && invLen >= 6 && invBuf[2] == 0x40 && invBuf[3] == 0x34) {
      size_t vl = invBuf[4] | (invBuf[5] << 8), pos = 6, end = 6 + vl < invLen ? 6 + vl : invLen;
      int c = 0;
      while (pos + 3 <= end && c < MAX_PRESETS) {
        uint8_t id = invBuf[pos], len = invBuf[pos + 2]; pos += 3;
        if (pos + len > end) break;
        presetIds[c] = id; presetNames[c] = String(std::string((char *)invBuf + pos, len).c_str()); pos += len; c++;
      }
      presetCount = c; dlog("presets: %d names", c); dirty = true;
    }
  }
  // amp-side preset selection report (0x3442: channel, preset)
  for (size_t o2 = 10; n >= 8 && o2 + 4 <= n;) {
    uint16_t ft = d[o2] | (d[o2 + 1] << 8), vl2 = d[o2 + 2] | (d[o2 + 3] << 8);
    if (o2 + 4 + vl2 > n) break;
    if (ft == 0x3442 && vl2 >= 2 && d[o2 + 4] == ampChannel) { presetNo = d[o2 + 5]; dirty = true; }
    o2 += 4 + vl2;
  }
  // tuner measurement (feature 0x3106): f32 freq, u8 string, f32 result, u8 signal
  bool tuner = false;
  size_t o = 10;  // after header(8) + status(2)
  while (n >= 8 && o + 4 <= n) {
    uint16_t feat = d[o] | (d[o + 1] << 8), vl = d[o + 2] | (d[o + 3] << 8);
    if (o + 4 + vl > n) break;
    if (feat == 0x0002 && cmd == 3) hbPending = true;  // answered from the main loop
    if ((cmd == 1 || cmd == 3) && vl >= 1 && (feat == 0x3140 || feat == 0x3145 || feat == 0x3148) && millis() - loopActMs > 600) {
      uint8_t v = d[o + 4]; bool rec = loopRecording, pl = loopPlaying, take = loopHasTake;
      if (feat == 0x3140 && v <= 3) rec = v != 0;        // 1..3 recording (first/second/third layer)
      if (feat == 0x3145 && v <= 3) pl = v == 1 || v == 3;
      if (feat == 0x3148) take = v > 0;
      if (rec != loopRecording || pl != loopPlaying || take != loopHasTake) {
        loopRecording = rec; loopPlaying = pl; loopHasTake = take; dirty = true;
        dlog("looper state: rec %d play %d layers>0 %d", rec, pl, take);
      }
      loopQuiet = true;
    }
    if (cmd == 3 && vl == 2 && feat == 0x3146 && d[o + 4] == 0 && d[o + 5] == 0) {  // loop wrapped
      uint32_t now = millis(), gap = now - loopWrapAt;
      if (loopWrapAt && (!loopPeriodMs || (gap > loopPeriodMs * 3 / 4 && gap < loopPeriodMs * 5 / 4))) loopPeriodMs = loopPeriodMs ? (loopPeriodMs + gap) / 2 : gap;
      loopWrapAt = now; loopQuiet = true; dirty = true;
    }
    if (feat == 0x3106 && vl == 10) {
      float f; memcpy(&f, d + o + 4, 4);
      tuner = true; tunerHz = d[o + 13] ? f : 0; tunerString = d[o + 8]; tunerMs = millis(); dirty = true;
    }
    if (feat == 0x3804 && vl >= 2 && d[o + 4] == 1 && d[o + 5] != 2 && tunerOn) { tunerOn = false; lastEvent = "tuner closed by amp"; dlog("EXIT: amp page state %u", d[o + 5]); }
    o += 4 + vl;
  }
  if (tuner) return;  // ~10 per second: keep out of the log
  if (loopQuiet) { loopQuiet = false; return; }  // looper poll replies: only state changes are logged
  char b[56]; int k = 0;
  size_t m = n > 24 ? 24 : n;
  k += snprintf(b, sizeof b, "N%u c%u ", (unsigned)n, cmd);
  for (size_t i = 8; i < m; i++) k += snprintf(b + k, sizeof b - k, "%02X", d[i]);
  lastNotify = b; dlog("%s", b);
  if (n > 24) { k = 0; for (size_t i = 24; i < n && i < 48; i++) k += snprintf(b + k, sizeof b - k, "%02X", d[i]); dlog("  +%s", b); }
}

static void startScan() {
  auto *s = NimBLEDevice::getScan();
  s->setScanCallbacks(&scanCb, true);
  s->setActiveScan(true);   // the name is in the scan response
  s->setInterval(100); s->setWindow(99);
  s->clearResults();
  s->start(0, false);
}

static bool bleConnect() {
  if (!client) {
    client = NimBLEDevice::createClient(); client->setClientCallbacks(&clientCb, false);
    client->setConnectTimeout(8000);
  }
  dlog("connecting...");
  if (!client->connect(foundAddr)) { dlog("connect failed e=%d", client->getLastError()); return false; }
  dlog("connected, discovering");
  auto *svc = client->getService(SERVICE_UUID);
  if (!svc) {
    dlog("service not found");
    for (auto *x : client->getServices(true)) dlog("svc %s", x->getUUID().toString().c_str());
    client->disconnect(); return false;
  }
  wChr = svc->getCharacteristic(WRITE_UUID);
  auto *nChr = svc->getCharacteristic(NOTIFY_UUID);
  dlog("write chr %s, notify chr %s", wChr ? "ok" : "MISSING", nChr ? "ok" : "MISSING");
  if (nChr && nChr->canNotify()) dlog("subscribe %s", nChr->subscribe(true, notifyCb) ? "ok" : "failed");
  if (!wChr) { client->disconnect(); return false; }
  bleState = B_CONNECTED; dlog("AMP READY"); needInit = true; return true;
}

static bool send(const uint8_t *f, size_t n) {
  if (bleState != B_CONNECTED || !wChr) { lastEvent += " (no amp)"; return false; }
  bool ok = wChr->writeValue(f, n, true);  // Write Request; the JBL app does the same
  if (!ok) { lastEvent += " (write failed)"; dlog("write failed"); }
  return ok;
}

// ---- actions -----------------------------------------------------------------------------
static void doAction(Action a) {
  uint8_t f[32]; size_t n = 0; char ev[40] = "";
  switch (a) {
    case ACT_PRESET_PREV: presetNo = presetNo <= 1 ? bandbox::PRESETS : presetNo - 1; goto preset;
    case ACT_PRESET_NEXT: presetNo = presetNo >= bandbox::PRESETS ? 1 : presetNo + 1;
    preset: n = bandbox::preset(f, ampChannel, presetNo); snprintf(ev, sizeof ev, "preset %d", presetNo); break;
    case ACT_TUNER: {
      tunerOn = !tunerOn; tunerMs = millis(); if (tunerOn) page = 0;
      snprintf(ev, sizeof ev, "tuner %s", tunerOn ? "on" : "off");
      lastEvent = ev; dirty = true;
      uint8_t p[16];
      if (tunerOn) {  // same order as the JBL One app: claim page, then start
        send(p, bandbox::pageInUse(p, 1, 2)); delay(150);
        for (uint16_t ft : {0x3102, 0x3101, 0x3104, 0x3103}) {  // app reads instrument, mode, string, reference first
          send(p, bandbox::readFeature(p, ft)); delay(100);
        }
        send(f, bandbox::tuner(f, true));
      } else {
        send(f, bandbox::tuner(f, false)); delay(150);
        send(p, bandbox::pageInUse(p, 1, 0));
      }
      delay(300); uint8_t r[10]; send(r, bandbox::readFeature(r, bandbox::F_TUNER));  // read-back, reply is logged
      return; }
    case ACT_LOOP_REC_STOP:
      loopActMs = millis();
      loopRecording = !loopRecording;
      if (loopRecording && !loopHasTake) loopRecAt = millis();
      if (!loopRecording) {
        if (!loopHasTake && loopRecAt) { loopPeriodMs = millis() - loopRecAt; loopWrapAt = millis(); }  // first estimate; wraps refine it
        loopHasTake = true; loopPlaying = true;
      }  // first take closed: loop plays, next press is a dub
      n = loopRecording ? bandbox::looperRecord(f) : bandbox::looperStop(f);
      snprintf(ev, sizeof ev, "loop %s", loopRecording ? "rec" : "stop"); break;
    case ACT_LOOP_PLAY_PAUSE:
      loopActMs = millis();
      loopPlaying = !loopPlaying;
      n = loopPlaying ? bandbox::looperPlay(f) : bandbox::looperPause(f);
      snprintf(ev, sizeof ev, "loop %s", loopPlaying ? "play" : "pause"); break;
    case ACT_LOOP_UNDO_CLEAR:
      if (millis() - lastUndoMs < CLEAR_WINDOW_MS) { lastUndoMs = 0; doAction(ACT_LOOP_CLEAR); return; }
      lastUndoMs = millis(); doAction(ACT_LOOP_UNDO); return;
    case ACT_LOOP_UNDO: n = bandbox::looperUndo(f); strcpy(ev, "loop undo"); break;
    case ACT_LOOP_CLEAR: loopActMs = millis(); loopHasTake = loopRecording = loopPlaying = false; loopPeriodMs = loopWrapAt = loopRecAt = loopFrozen = 0; n = bandbox::looperClear(f); strcpy(ev, "loop clear"); break;
    case ACT_BOOST: {
      boostOn = !boostOn;
      const uint8_t *src = boostOn ? BOOST_ON : BOOST_OFF; size_t len = boostOn ? sizeof BOOST_ON : sizeof BOOST_OFF;
      if (!len) { lastEvent = "boost: not set"; dirty = true; return; }
      lastEvent = boostOn ? "boost on" : "boost off"; send(src, len); dirty = true; return; }
    case ACT_PAGE_TOGGLE: page ^= 1; lastEvent = String("page ") + PAGE_NAMES[page]; dirty = true; return;
    default: return;
  }
  lastEvent = ev; send(f, n); dirty = true;
  Serial.println(lastEvent);
}

static void onPedalKey(int key) {
  if (key == 4) { doAction(ACT_PAGE_TOGGLE); return; }  // page toggle key
  if (key < 0 || key > 3) return;
  uint32_t now = millis();
  if (now - lastKeyMs[key] < DEBOUNCE_MS) return;
  lastKeyMs[key] = now; doAction(PAGES[page][key]);
}

// ---- USB MIDI in -------------------------------------------------------------------------
#include <Preferences.h>
static BridgeSettings settings = {true, EXPR_INVERT};
BridgeSettings &bridgeSettings() { return settings; }
void bridgeSaveSettings() {
  Preferences pr; if (!pr.begin("bridge", false)) return;
  pr.putBool("exprOn", settings.exprEnabled); pr.putBool("exprInv", settings.exprInvert); pr.end();
}
static void loadSettings() {
  Preferences pr; if (!pr.begin("bridge", true)) return;
  settings.exprEnabled = pr.getBool("exprOn", settings.exprEnabled);
  settings.exprInvert = pr.getBool("exprInv", settings.exprInvert); pr.end();
}

static int exprPending = -1, exprSent = -1; static uint32_t exprMs = 0;  // level 0..32 waiting to be sent

static void pollMidi() {
  midiEventPacket_t p;
  while (MIDI.readPacket(&p)) {
    usbSeen = true; dirty = true;
    uint8_t cin = p.header & 0x0F, d1 = p.byte2, d2 = p.byte3;
    MidiKind k; bool press;
    if (cin == 0xB) { k = M_CC; press = d2 > 0; }
    else if (cin == 0xC) { k = M_PC; press = true; }
    else if (cin == 0x9) { k = M_NOTE; press = d2 > 0; }
    else continue;
    if (cin == 0xB && d1 == EXPR_CC) { if (settings.exprEnabled) exprPending = ((settings.exprInvert ? 127 - d2 : d2) * bandbox::MIXER_STEPS + 63) / 127; continue; }  // not logged: streams
    dlog("MIDI %X %02X %02X", cin, d1, d2);
    if (k == M_CC && d1 == 100 && d2 == 127) { uint8_t r[10]; send(r, bandbox::readFeature(r, bandbox::F_TUNER)); dlog("read 3100"); }
    // debug reads (harmless): CC101 v = page_in_use(page v: 1 tuner,2 looper,3 drum,4 metronome); CC105 v = read feature 0x3100+v
    if (k == M_CC && d1 == 101 && d2 >= 1 && d2 <= 4) { uint8_t r[16]; send(r, bandbox::frame1(r, 0x3803, d2)); dlog("read page %u", d2); }
    if (k == M_CC && d1 == 107 && d2 == 127) { uint8_t r[16]; send(r, bandbox::frame1(r, 0x3441, ampChannel)); dlog("read preset"); }
    if (k == M_CC && d1 == 105 && d2 <= 127) { uint8_t r[10]; send(r, bandbox::readFeature(r, 0x3100 + d2)); dlog("read 31%02X", d2); }
    if (k == M_CC && d1 == 119 && d2 == 127) { dlog("rebooting to download mode"); delay(300); usb_persist_restart(RESTART_BOOTLOADER); }
    if (!press) continue;
    for (const auto &m : INPUT_MAP) if (m.kind == k && m.number == d1) { onPedalKey(m.key); break; }
  }
}

// ---- glue --------------------------------------------------------------------------------
static const char *actionLabel(Action a) {
  switch (a) {
    case ACT_PRESET_PREV: return "Pr-"; case ACT_PRESET_NEXT: return "Pr+";
    case ACT_BOOST: return "Boost"; case ACT_TUNER: return "Tune";
    case ACT_LOOP_REC_STOP: return loopRecording ? "Stop" : loopHasTake ? "Dub" : "Rec"; case ACT_LOOP_PLAY_PAUSE: return loopPlaying ? "Pause" : "Play";
    case ACT_LOOP_UNDO_CLEAR: return "Undo\nClr"; case ACT_LOOP_UNDO: return "Undo";
    case ACT_LOOP_CLEAR: return "Clear"; case ACT_PAGE_TOGGLE: return "Page"; default: return "-";
  }
}

static void drawNow() {
  BridgeStatus s;
  s.page = page; s.pageName = PAGE_NAMES[page]; s.presetNo = presetNo; s.ble = bleState;
  s.usbSeen = usbSeen; s.lastEvent = lastEvent; s.lastNotify = lastNotify; s.ampName = ampName;
  s.ampChannel = ampChannel;
  s.presetName = "";
  for (int i = 0; i < presetCount; i++) if (presetIds[i] == presetNo) { s.presetName = presetNames[i]; break; }
  s.tunerOn = tunerOn; s.tunerString = tunerString;
  s.tunerHz = (millis() - tunerMs < 600) ? tunerHz : 0;
  s.loopState = loopRecording ? (loopHasTake ? "DUB" : "REC") : !loopHasTake ? "EMPTY" : loopPlaying ? "PLAY" : "PAUSE";
  s.loopLenMs = loopPeriodMs; s.loopSecs = loopPeriodMs / 1000.0f; s.loopPosMs = 0;
  if (loopPeriodMs && loopHasTake) {
    uint32_t ph = (loopPlaying || loopRecording) ? (millis() - loopWrapAt) % loopPeriodMs : loopFrozen;
    s.loopPosMs = loopFrozen = ph;
  }
  for (int i = 0; i < 4; i++) s.labels[i] = actionLabel(PAGES[page][i]);
  s.logCount = logN; for (int i = 0; i < logN; i++) s.log[i] = logLines[i];
  dirty = false;
  ui->draw(s);
}

static RTC_NOINIT_ATTR uint32_t bootMagic, bootCount;

void bridgeBegin(BridgeUi *u) {
  ui = u; loadSettings();
  ui->begin();
  Serial.begin(115200);
  // reset diagnostics: visible on screen even when running from the pedal's USB port (1 poweron, 4 panic, 6 brownout...)
  if (bootMagic != 0xB007B007) { bootMagic = 0xB007B007; bootCount = 0; }
  bootCount++;
  dlog("boot #%u reset=%d", (unsigned)bootCount, (int)esp_reset_reason());
  USB.productName("Chocolate+ Bridge"); USB.manufacturerName("lucanenni");
  MIDI.begin(); USB.begin();
  NimBLEDevice::init("choc-bridge");
  startScan();
}

void bridgeLoop() {
  pollMidi();
  int t = ui->pollTouch();
  if (t >= 0 && t < 4) onPedalKey(t); else if (t == -2) dirty = true;  // UI changed its own view (debug screen toggle)
  if (bleState == B_FOUND) {
    drawNow();
    if (!bleConnect()) { bleState = B_SCAN; startScan(); }
    dirty = true;
  } else if (bleState == B_SCAN && !NimBLEDevice::getScan()->isScanning()) {
    startScan();
  }
  if (tunerOn && millis() - tunerMs > 10000) { tunerOn = false; lastEvent = "tuner stopped (no data)"; dlog("EXIT: no data 10s"); }
  if (exprPending >= 0 && exprPending != exprSent && millis() - exprMs >= EXPR_MIN_INTERVAL_MS && bleState == B_CONNECTED) {
    uint8_t v[20]; exprMs = millis(); exprSent = exprPending;
    if (send(v, bandbox::channelLevel(v, ampChannel, EXPR_INPUT_TYPE, exprSent))) { lastEvent = String("vol ") + exprSent; dirty = true; }
  }
  if (needInit && bleState == B_CONNECTED) {  // names and current selection, once per connection
    needInit = false; uint8_t q[16];
    send(q, bandbox::readFeature(q, 0x3440)); delay(200);
    send(q, bandbox::frame1(q, 0x3441, ampChannel));
  }
  if (page == 1 && !tunerOn && bleState == B_CONNECTED && millis() - loopPollMs >= 250) {  // follow the amp's own looper state
    loopPollMs = millis(); uint8_t q[16];
    static const uint16_t pf[3] = {0x3140, 0x3145, 0x3148};
    send(q, bandbox::readFeature(q, pf[loopPollStep])); loopPollStep = (loopPollStep + 1) % 3;
    dirty = true;
  }
  if (hbPending) { hbPending = false; uint8_t h[16]; if (bleState == B_CONNECTED && wChr) wChr->writeValue(h, bandbox::heartbeatReply(h), true); }
  static uint32_t lastDraw = 0;
  if (dirty && millis() - lastDraw > 50) { drawNow(); lastDraw = millis(); }
  delay(2);
}
