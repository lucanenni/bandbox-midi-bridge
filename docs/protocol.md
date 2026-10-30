# BandBox protocol, as used by the bridge

Hardware-tested on a BandBox Solo (channel 1; Trio uses 3) unless noted. Complements bandbox-studio's `docs/protocol.md`.

## Transport
- Service `65786365-6c70-6f69-6e74-2e636f6d0000`, write `...0002` (Write Request), notify `...0001`. The name is only in the scan response (active scan, duplicates on). No keepalive is needed; the amp reconnects by itself after the bridge reboots while its Bluetooth stays on.
- Frame: `00 DD | cmd:2 LE | count | index | len:2 | feature:2 LE | vlen:2 | value`, `len = 4 + vlen`. Commands: 1 read, 2 set, 3 amp notification, 4 notify-to-device (heartbeat reply). Write ack: `c2 0000` (status 0). Read replies come as c1 `status:2 feature:2 vlen:2 value`; long ones are fragmented (count/index).

## Features used
| Feature | Meaning |
|---|---|
| `0x3441` [ch] / `0x3442` [ch, preset] | current selection query / report and select (1..27) |
| `0x3440` | preset inventory (read): repeated `[id, genre, name length, UTF-8 name]` |
| `0x3140` | looper: 1 rec, 0 stop, 4 undo, 5 clear |
| `0x3145` | looper playback: 3 resume/play, 2 pause |
| `0x3406` [ch, input type, level] | mixer level, raw steps 0..32; input type 2 = guitar (confirmed on the Solo's channel 1) |
| `0x3803` [page] / `0x3804` [page, state] | page in use: 1 tuner, 2 looper, 3 drum, 4 metronome; state 0 none, 1 device, 2 app |
| `0x3100`..`0x3107` | tuner family |
| `0x0002` | heartbeat |
| `0x000D` | battery status |

## Tuner session
Found by decompiling JBL One 2.1.16. A bare `0x3100=1` is acked but ignored. The working sequence:
1. `0x3804 = [1, 2]` (claim the tuner page for the app).
2. Read `0x3102` (instrument), `0x3101` (string mode), `0x3104` (tuning), `0x3103` (reference pitch).
3. `0x3100 = 1`.
4. The amp streams `0x3106` about 10 times a second: `f32 frequency | u8 string | f32 result | u8 signal` (little endian). Zeros mean no signal. String 1 = high E ... 6 = low E in standard tuning.
5. Stop: `0x3100 = 0`, then `0x3804 = [1, 0]`.
`0x3102`: 0 guitar 3+3, 1 guitar 6-in-line, 2 bass, 3 ukulele. `0x3101`: 0 auto, 1..6 fixed string.

**Heartbeat.** The amp notifies feature `0x0002` periodically; the host answers with a cmd-4 frame carrying feature `0x0002` and no value: `00 DD 04 00 01 00 04 00 02 00 00 00`. Without the answer the amp leaves the tuner page after a few seconds. The amp is muted while the tuner is open.

## Mixer level
`0x3406 [channel, 2, level]` with level 0..32. The amp echoes the state as a c3 notification. The expression pedal maps CC 7 (0..127) to 0..32, at most one write per 50 ms, only when the value changes.

## Preset names
Read `0x3440` once per connection. The id sent to `0x3442` is treated as the inventory id; names are matched by id. If a name does not match the amp's, match by list position instead (to verify).

## Notifications seen
`0x000D` battery, `0x2D42` unknown, `0x3142` looper signal, `0x3406` mixer echo, `0x3804` page state, `0x3442` selection, cmd-4 keepalive.

## Looper state (read)
`0x3140` read = recording state (0 stop, 1/2/3 recording first/second/third layer, 4 undo, 5 discard), `0x3145` = playback (0 stop, 1 play, 2 pause, 3 resume), `0x3148` = layer count, `0x3147` = bars (4 seen). The bridge polls these three every 250 ms on the looper page; `0x3140` is not notified spontaneously. An overdub returns to 0 by itself at the end of the phrase.

`0x3143` (length) and `0x3146` (position) are **not time**: 3214 for a ~9.6 s loop, 500 for a ~12 s loop. Read replies also arrive in bursts, so wall-clock timing of polled values is useless. `0x314A` stayed 1333 across loops (meaning unknown); `0x3141`/`0x3149` gave no reply.

What does work: the amp pushes a notification `0x3146 = 0` (cmd 3) at every loop wrap. The bridge averages the time between wraps as the loop duration (first estimate: Rec to Stop press) and computes the phase locally for the progress bar; confirmed on hardware.
