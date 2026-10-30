// BandBox frame builders (pure C++, no Arduino deps so they can be unit-tested on the host).
// Frame: 00 DD | cmd:2 LE | count | index | len:2 LE | feature:2 LE | vlen:2 LE | value
// len = 4 + vlen. Layout validated against bandbox-studio (see docs/bandbox-feasibility.md).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace bandbox {

constexpr uint16_t F_TUNER = 0x3100;
constexpr uint16_t F_PAGE_IN_USE = 0x3804;  // set: [page, state]; page 1 tuner; state 0 none, 1 device, 2 app
constexpr uint16_t F_LOOPER_CTRL = 0x3140;   // 01 rec, 00 stop, 04 undo, 05 clear
constexpr uint16_t F_LOOPER_PLAY = 0x3145;   // 03 play, 02 pause
constexpr uint16_t F_PRESET = 0x3442;        // [channel, preset 1..27]

constexpr int PRESETS = 27;
constexpr uint16_t F_MIXER_LEVEL = 0x3406;   // [channel, input type, level 0..32]
constexpr int MIXER_STEPS = 32;

inline size_t frameCmd(uint8_t *out, uint8_t cmd, uint16_t feature, const uint8_t *value, uint16_t vlen) {
  size_t n = 0;
  out[n++] = 0x00; out[n++] = 0xDD;
  out[n++] = cmd; out[n++] = 0x00;                        // cmd 2 = set, 4 = notify to device
  out[n++] = 0x01;                                        // count
  out[n++] = 0x00;                                        // index
  uint16_t len = 4 + vlen;
  out[n++] = len & 0xFF; out[n++] = len >> 8;
  out[n++] = feature & 0xFF; out[n++] = feature >> 8;
  out[n++] = vlen & 0xFF; out[n++] = vlen >> 8;
  for (uint16_t i = 0; i < vlen; i++) out[n++] = value[i];
  return n;
}

inline size_t frame(uint8_t *out, uint16_t feature, const uint8_t *value, uint16_t vlen) {
  return frameCmd(out, 0x02, feature, value, vlen);
}

// The amp notifies feature 0x0002 (heartbeat) about every 5 s; JBL One answers with cmd 4, feature 0x0002, no value.
// Without the answer the amp drops out of the tuner page after a few seconds.
inline size_t heartbeatReply(uint8_t *out) { return frameCmd(out, 0x04, 0x0002, nullptr, 0); }

inline size_t frame1(uint8_t *out, uint16_t feature, uint8_t v) { return frame(out, feature, &v, 1); }

// Read request (cmd 1): 00 DD 01 00 01 00 02 00 <feature:2 LE>
inline size_t readFeature(uint8_t *out, uint16_t feature) {
  const uint8_t h[8] = {0x00, 0xDD, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00};
  for (int i = 0; i < 8; i++) out[i] = h[i];
  out[8] = feature & 0xFF; out[9] = feature >> 8; return 10;
}

// channel: 3 on the Trio, 1 on the Solo; preset is 1-based.
inline size_t preset(uint8_t *out, uint8_t channel, uint8_t preset1) {
  uint8_t v[2] = {channel, preset1};
  return frame(out, F_PRESET, v, 2);
}
// JBL One claims the page before starting the tuner and releases it after stopping.
inline size_t pageInUse(uint8_t *out, uint8_t page, uint8_t state) {
  uint8_t v[2] = {page, state};
  return frame(out, F_PAGE_IN_USE, v, 2);
}
// Channel level in raw mixer steps 0..32. Input type 2 = guitar on the Trio's channel 3.
inline size_t channelLevel(uint8_t *out, uint8_t channel, uint8_t inputType, uint8_t level) {
  uint8_t v[3] = {channel, inputType, level};
  return frame(out, F_MIXER_LEVEL, v, 3);
}
inline size_t tuner(uint8_t *out, bool on) { return frame1(out, F_TUNER, on ? 1 : 0); }
inline size_t looperRecord(uint8_t *out) { return frame1(out, F_LOOPER_CTRL, 0x01); }
inline size_t looperStop(uint8_t *out) { return frame1(out, F_LOOPER_CTRL, 0x00); }
inline size_t looperUndo(uint8_t *out) { return frame1(out, F_LOOPER_CTRL, 0x04); }
inline size_t looperClear(uint8_t *out) { return frame1(out, F_LOOPER_CTRL, 0x05); }
inline size_t looperPlay(uint8_t *out) { return frame1(out, F_LOOPER_PLAY, 0x03); }
inline size_t looperPause(uint8_t *out) { return frame1(out, F_LOOPER_PLAY, 0x02); }

}  // namespace bandbox
