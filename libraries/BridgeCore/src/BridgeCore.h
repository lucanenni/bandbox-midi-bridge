// UI-independent bridge logic: USB-MIDI in (pedal), BLE central out (BandBox), actions, state.
// A front-end implements BridgeUi (display + touch) and calls bridgeBegin / bridgeLoop.
#pragma once
#include <Arduino.h>

enum BleState { B_SCAN, B_FOUND, B_CONNECTED };

struct BridgeStatus {
  int page;                 // 0..1
  const char *pageName;
  int presetNo;
  BleState ble;
  bool usbSeen;
  String lastEvent, lastNotify, ampName, presetName;  // presetName empty until the amp's list is read
  uint8_t ampChannel;
  String log[8];            // newest last; diagnostics (BLE steps, MIDI in, notifications)
  int logCount;
  bool tunerOn;
  float tunerHz;            // 0 = no signal
  int tunerString;          // amp's string number 1..6
  const char *loopState;    // EMPTY REC DUB PLAY PAUSE
  uint32_t loopLenMs, loopPosMs;  // recorded length and position in the amp's units (use as a fraction), 0 = unknown
  float loopSecs;                 // measured loop duration in seconds, 0 = not measured yet
  const char *labels[4];    // action label of each pedal key on the current page
};

class BridgeUi {
 public:
  virtual ~BridgeUi() {}
  virtual void begin() = 0;                       // init display/touch (called first)
  virtual void draw(const BridgeStatus &s) = 0;   // redraw when state changed
  virtual int pollTouch() = 0;                    // 0..3 = key A..D, -2 = UI view changed (redraw), -1 = none
};

// User settings, kept in flash (NVS). A front-end edits them and calls bridgeSaveSettings().
struct BridgeSettings {
  bool exprEnabled;   // expression pedal (CC EXPR_CC) controls the guitar level
  bool exprInvert;    // flip the pedal direction
};
BridgeSettings &bridgeSettings();
void bridgeSaveSettings();

void bridgeBegin(BridgeUi *ui);
void bridgeLoop();
