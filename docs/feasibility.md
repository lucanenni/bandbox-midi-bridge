# JBL BandBox from the Chocolate Plus: feasibility (analysis only, nothing flashed)

Sources: the amp's protocol from the sibling repo `~/Claude_sessions/bandbox-studio` (`docs/protocol.md`, `re/PROTOCOL.md`, `webble/`; validated on a Trio and a Solo), the earlier notes and the draft patch on the `bandbox` branch of this repo (`docs/bandbox.md`, `bandbox_patch.py`), and this repo's firmware analysis (`FINDINGS.md`).

## 1. How to attach to the amp
- The BandBox is a **BLE peripheral**: custom service `65786365-6C70-6F69-6E74-2E636F6D0000`, **write** characteristic `…0002` (props `0x0C`: write + write-without-response), **notify** characteristic `…0001` (`0x12`: read + notify). It advertises `JBL BandBox Trio` / `Solo` **in the scan response** (the ADV_IND has only service data), and only after its Bluetooth button is pressed. Control from a Mac (bleak, CoreBluetooth, Web Bluetooth) works without bonding; the JBL One app writes with Write Request after enabling the CCCD.
- **The pedal must therefore be the central.** The stock firmware already is one: at boot it scans, matches an advertised name by **substring** (`strstr(name, "Spark")` / `"spark"`, scan responses included: `filter_pdu_bitmap = 0`), connects, discovers its target characteristics by UUID from a 2-record table (`target_uuid_t`, 16-bit fields 0 = use the 128-bit ones) and writes ATT **Write Commands** (op 6, a one-byte patch makes it op 5 = Write Request). The central/128-bit-service path was **verified live** (the pedal connected to a BLE-MIDI peripheral advertised by a Mac at power-on).
- The amp-control path exists as a feature: global mode 12 **Speaker Control** (`0x13802`): when the BLE client link is up (flag `S+0x110`), a tap on key k enqueues event `k`, `k+4` or `k+8` (a 3-state toggle per key) into a 32-entry queue (`0x137E4`); the writer (`0x14514`) pops it, looks up `frame = table[event]` (12 length-prefixed frames at file `0x18360`, pointers at `0x18480 - ...`), copies it to the TX buffer and writes it through `0x144EC`, resending until the write is accepted; if the link is down the same keys send the USB-MIDI SysEx `F0 73 6B 73 70 51 50 50 <n> 01 F7`. So "4 footswitches -> fixed frames to a BLE amp" is what mode 12 does today for its original target.

## 2. What can be sent (BandBox protocol, little endian)
Frame to the write characteristic: `00 DD | cmd:2 | count:1 | index:1 | len:2 | feature:2 | vlen:2 | value` (preset select: `00 DD 02 00 01 00 06 00  42 34  02 00  <channel> <preset>`, channel 3 on the Trio, 1 on the Solo, preset = 1-based flat index of the 27 factory presets).
| action | feature / value (all validated on hardware unless noted) |
|---|---|
| select preset | `0x3442` `[channel, preset]` (also `0x3443` detail query) |
| looper | `0x3140`: `01` record/start, `00` stop, `04` undo (effect unconfirmed), `05` clear; `0x3145`: `03` play/resume, `02` pause |
| effects | `0x3445` model / parameter / active-bypass for the blocks GAT, COM, WAH, AMP, CAB, MOD, DLY, RVB (byte-verified, not exercised on an amp) |
| mixer | `0x3405/0x3406` levels `0..32` (needs a matching input report first) |
| tuner / metronome / drums | families `0x304x`, `0x300x`, `0x310x` (named controls only) |
| save / duplicate / rename preset | `0x3446` family (byte-verified) |
The amp echoes on notify (command 3) and sends a frequent `04` keepalive frame; whether the controller must send keepalives is untested.

## 3. Verdict
**Technically plausible, with three unknowns that only a flash can answer; a no-firmware route exists and should come first.**
Firmware route (needs the recovery dongle, see `docs/REPORT.md`):
1. *Data-only part (small patch, same lengths, what `bandbox_patch.py` drafts):* name `Spark` -> `JBL B`, replace the Spark target record with the BandBox service + write characteristic (props `0x0C`), replace the 12 frames. Gives 12 **fixed** commands (e.g. four keys x three toggle states: looper record/stop/play, undo/clear, presets 1-4...).
2. *Needs new code (we now have the compiler):* dynamic frames (preset = f(bank, key), "next/prev preset", an effect parameter from the analog input), because the table is static; a keepalive timer if the amp needs one.
3. *Unknowns:* (a) the amp's notify characteristic must be subscribed if the amp requires it, but the build has only **two** target records and one is the BLE-MIDI record (give up BLE-MIDI central to free it; auto-CCCD happens for a record with the NOTIFY property); (b) keepalive; (c) Write Command vs Write Request acceptance by the amp (props allow both; the draft already patches to op 5). None of these can be tested without flashing. The scan trigger is boot-time only (the stock firmware scans at power-on until a target connects): the amp must be advertising when the pedal powers on.
Costs/risks: needs the USB-boot recovery hardware before any flash; Speaker Control mode is the natural host but the pedal then no longer works as the Spark controller; 013 has different offsets.
**No-firmware route (works today, no risk):** the pedal already emits MIDI over USB (Program Switch A: `C0 n`; Advanced Custom: arbitrary messages and SysEx), so a small bridge on the Mac that maps those messages to BandBox frames (reuse `bandbox-studio`: `preset_controls.py`, `looper_controls.py`, `webble/bandbox-ble.js`) gives the same functions without touching the pedal. A standalone bridge (ESP32 or Pico W as BLE central + USB host) would make it computer-free, but needs extra hardware and USB-host code.
Suggested order: (1) Mac bridge with Program Switch A mapping (preset N) + Advanced Custom keys for looper; (2) if a computer-free pedal is wanted, buy the recovery dongle, test it on an official image, then try the data-only patch on 012 with `bandbox_patch.py` before writing any new code.
