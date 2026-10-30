# bandbox-midi-bridge

Control a **JBL BandBox** amp (Solo, Trio) from an **M-Vave Chocolate Plus** footswitch, with no change to the pedal's firmware.

```
Chocolate Plus (USB host, switch on H, mode 4)
   -> USB-C -> ESP32-S3 as USB-MIDI device
   -> BLE central -> BandBox
```

The Chocolate sends MIDI over USB; the ESP32 turns it into the BandBox's BLE control protocol, shows the state on its screen, and has its own touch buttons. Front-ends: **M5Stack CoreS3** (tested) and **Waveshare ESP32-S3-Touch-LCD-3.49 V2** (compiles, not yet tested on hardware).

## What works (CoreS3 + BandBox Solo + Chocolate Plus)
| Function | Status |
|---|---|
| Preset previous/next, name and number on screen, follows the amp's own selection | works |
| Looper: rec / dub / stop, play/pause, undo (twice quickly = clear); state (REC, DUB, PLAY, PAUSE) and progress bar on screen | works, also from the pedal |
| Tuner: note, Hz and cents on screen, amp streams measurements | works |
| Expression pedal (TRS) -> guitar channel level (CC 7, 0..127 -> 0..32), optional inversion | works |
| Page toggle (pedal key D or touch), debug log screen, settings page (gear icon) | works |
| Boost | dropped: frame never found, not planned |

## Pedal setup
Set the Chocolate Plus to **mode 4** (looper mode): the keys send CC 20..23 with 127 on press and 0 on release. Program Switch mode (PC 0..3) is also mapped but untested. Set the TRS input to send **CC 7** with values 0..127. All mapping is in `libraries/BridgeCore/src/config.h`.

Default pages (keys A..D):

| Page | A | B | C | D |
|---|---|---|---|---|
| 0 | Pr- | Pr+ | Tune | Page |
| 1 (Loop) | Rec / Stop | Play / Pause | Undo / Clr | Page |

Button labels follow the amp's looper state, polled every 250 ms (a loop started from the amp or app is seen too). The progress bar is computed locally from the amp's wrap notifications, see `docs/protocol.md`.

## Screens (CoreS3)
- **Main**: BLE and USB status, page name, preset number and name, last event, five touch buttons (A..D and Log).
- **Tuner**: appears when the tuner is on. Nearest note, Hz, cents and a deviation bar; Exit toggles it off.
- **Log**: diagnostic strings (BLE steps, MIDI in, notifications, boot count and reset reason). Gear icon top right opens **Settings**: expression pedal on/off, expression invert. Settings are stored in flash.

## Layout
- `libraries/BridgeCore/` shared logic: USB-MIDI in, BLE out, actions, settings. `src/config.h` mapping, `src/bandbox.h` frame builders, `test/` host tests.
- `cores3/` M5Stack CoreS3 front-end (320x240, M5Unified).
- `waveshare349/` Waveshare **V2** front-end (640x172 landscape, LVGL 9.3). Drivers are Waveshare's V2 example (`LICENSE-waveshare`, Apache-2.0); changes: demo removed, lock functions exported, rotation 90. V1 boards have a different backlight pin: do not flash this on a V1. No gear/settings or tuner screen yet.
- `docs/` protocol notes and the feasibility study.

## Build and flash
Libraries: NimBLE-Arduino 2.5.0, M5Unified 0.2.13, M5GFX 0.2.19, lvgl 9.3.0, core esp32 3.3.12 (Waveshare asks 3.3.0). LVGL needs `lv_conf.h` next to the libraries folder: `cp waveshare349/lv_conf.h ~/Documents/Arduino/libraries/`.
```
# CoreS3
arduino-cli compile --upload -p <port> --libraries libraries --fqbn esp32:esp32:m5stack_cores3:USBMode=default,CDCOnBoot=cdc cores3
# Waveshare 3.49
arduino-cli compile --libraries libraries --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=default,CDCOnBoot=default waveshare349
```
Host test of the frame builders: `g++ -std=c++17 -o /tmp/tb libraries/BridgeCore/test/test_bandbox.cpp && /tmp/tb`.

Reflashing: the device enumerates as USB-MIDI, so there is no serial port to reset through. Send **CC 119 = 127** to the "Chocolate+ Bridge" MIDI port: it reboots into the ROM bootloader and flashes without buttons. If the board is stuck in download mode after a manual entry, press reset once. `CDCOnBoot=cdc` adds a serial port for the log (115200).

Power: the CoreS3 runs from its own battery when plugged into the pedal.

## Debug MIDI CCs (value 127 unless stated)
CC100 read `0x3100`; CC101 v read page-in-use v (1..4); CC105 v read feature `0x3100+v`; CC107 read current preset; CC119 reboot to bootloader.

## Protocol
See `docs/protocol.md`: frames, preset, looper, tuner session (page claim, measurements, heartbeat), mixer level, preset inventory. Complements [bandbox-studio](https://github.com/lucanenni/bandbox-studio) (Mac GUI/MCP for the same amp).

## Known limits and open items
- Boost is out of scope (frame never found). The pedal's own ADC range cannot be reversed, hence the bridge-side invert option.
- The Solo can advertise with no name (see bandbox-studio notes); the bridge matches by name "BandBox", so that state would not be found.
- Waveshare 3.49: hardware test, touch under rotation, tuner screen, settings.
- The reset loop seen once when first connected to the pedal's USB-A is unexplained; it disappeared after reflashing with boot diagnostics (boot count and reset reason are shown in the log).

## License
MIT (see `LICENSE`). The Waveshare display drivers in `waveshare349/` are Waveshare's, Apache-2.0 (`waveshare349/LICENSE-waveshare`).
