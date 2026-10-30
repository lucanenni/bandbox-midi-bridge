// Mapping and tunables. Edit here; no other file needs to change.
#pragma once
#include <stdint.h>

enum Action : uint8_t {
  ACT_NONE, ACT_PRESET_PREV, ACT_PRESET_NEXT, ACT_BOOST, ACT_TUNER,
  ACT_LOOP_REC_STOP,    // toggle: record <-> stop
  ACT_LOOP_PLAY_PAUSE,  // toggle: play <-> pause
  ACT_LOOP_UNDO_CLEAR,  // press = undo, second press within CLEAR_WINDOW_MS = clear
  ACT_LOOP_UNDO, ACT_LOOP_CLEAR, ACT_PAGE_TOGGLE,
};

// Pedal key (0..3 = A..D) -> action, per page.
static const Action PAGES[2][4] = {
  {ACT_PRESET_PREV,     ACT_PRESET_NEXT,      ACT_TUNER,           ACT_PAGE_TOGGLE},
  {ACT_LOOP_REC_STOP,   ACT_LOOP_PLAY_PAUSE,  ACT_LOOP_UNDO_CLEAR, ACT_PAGE_TOGGLE},
};
static const char *PAGE_NAMES[2] = {"PRESETS", "LOOPER"};

// Incoming USB-MIDI message -> pedal key. Any channel. A CC counts as a press when value > 0.
// Defaults match the pedal's built-in looper mode (CC 20..23) and Program Switch (PC 0..3).
enum MidiKind : uint8_t { M_CC, M_PC, M_NOTE };
struct InMap { MidiKind kind; uint8_t number; uint8_t key; };
static const InMap INPUT_MAP[] = {
  {M_CC, 20, 0}, {M_CC, 21, 1}, {M_CC, 22, 2}, {M_CC, 23, 3},
  {M_PC, 0, 0},  {M_PC, 1, 1},  {M_PC, 2, 2},  {M_PC, 3, 3},
  {M_CC, 24, 4},  // key index 4 = page toggle (e.g. long press of D)
};

// Amp selection. Scan matches this substring of the advertised name.
static const char *AMP_NAME_MATCH = "BandBox";
static const uint8_t CHANNEL_TRIO = 3, CHANNEL_SOLO = 1;  // preset-select channel byte

static const uint32_t CLEAR_WINDOW_MS = 700;
// Expression pedal (Chocolate TRS input): CC 0..127 -> guitar channel level 0..32
static const uint8_t EXPR_CC = 7, EXPR_INPUT_TYPE = 2;
static const uint32_t EXPR_MIN_INTERVAL_MS = 50;
static const bool EXPR_INVERT = true;  // the Chocolate's TRS input reports 0 with the pedal down; flip so down = loud
static const uint32_t DEBOUNCE_MS = 120;  // ignore a repeated key within this window

// ACT_BOOST is not mapped on any page. It has no native BandBox model; supply raw frames (hex captured from the JBL app or
// bandbox-studio effect_controls) to enable it. Leave empty to show "boost: not set".
static const uint8_t BOOST_ON[] = {};
static const uint8_t BOOST_OFF[] = {};
