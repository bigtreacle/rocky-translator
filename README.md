# Eridian Translator

A handheld desk toy that speaks in chords and listens back, on the Waveshare
ESP32-S3-Touch-AMOLED-1.8. `rocky-translator-brief.md` is the brief,
`DESIGN.md` the design and build plan. State: **M0 bring-up, M2 translator
screen, M3 phrases from JSON, M1 sound, M4 listening and M5 motion and
power done** (tested on a V2 board, 2026-11-08/09; see the status notes in
DESIGN.md §14). Pick it up and it greets you, shake it and it complains,
turn it face down and it sleeps; it dims after a minute and switches the
screen off after five. The power button short-press toggles the screen,
a long press powers off. It listens:
talk to it and it replies; whistle a note and it shows the note and sings it
back. Sound (M1) works: a short press of the
BOOT side button toggles the speaker, the setting is remembered, and the
volume defaults to 60 (`vol 45` over serial to change it; it is saved).

## Build and flash

PlatformIO with the official `espressif32` platform 8.x (Arduino core 3.3).

    pio run -e amoled18 -t upload
    pio device monitor

Close the monitor before flashing. The board does not leave download mode by
itself: power-cycle it (or hold PWR 6 s) after a flash if it sits silent.

## What the screen shows now

The translator console: status word and battery in a thin top strip, a 24-bar
spectrum, the notes being "played", and the English line typing out below.
Tap the screen for the next phrase of an exchange. Until the mic and IMU are
wired in, these serial commands stand in for them:

    tap   speech   whistle 72   pickup   shake   shift   b 40   reload   phrases   m0
    mute   vol 70   audio   mic   micpeak   listen 0|1   sens 10
    imu   imulog 1   timers 1   standby   off   wake   bright 80

## Writing phrases

Edit `data/phrases.json` (the file explains its fields), check it, upload it.
No firmware rebuild is needed:

    python3 tools/check_phrases.py
    pio run -e amoled18 -t uploadfs

Then `reload` over serial, or reset the board. Keep notes in 60..84 (C4..C6)
for the small speaker; the checker warns about others.

(`m0` brings back the hardware test screen below; reset to return.)

## What the M0 test screen shows (`m0`)

Serial (USB CDC, 115200): chip, flash and PSRAM sizes (expect 16 MB and
about 8192 KB), the I2C scan with device names, the detected board revision
(V1 SH8601 + FT3168, or V2 CO5300 + CST820), then one status line per second
with accelerometer, battery and power-button state.

Screen: four colour bars (red, green, blue, white), a 1 px white frame at the
very edge, a small accent rectangle at odd coordinates (tests the even-pixel
rule), the firmware version and revision text, the live status block, and an
amber dot that follows a finger.

## Day-one checks (see DESIGN.md §14 and Appendix A)

1. Which revision the log reports, and a photo of the PCB markings.
2. (Done 2026-11-08: I2C is SDA 15 / SCL 14; the board is V2.)
3. (Done: touch is quick with INT gating; no x offset needed on this V2 board.
   It lands slightly low; measure and add a y offset if it bothers you.)
4. GPIO 46 at 3.3 V with a meter after boot.
5. Whether the TCA9554's INT output reaches an ESP32 pin (schematic).
6. (Done: bezel corners are rounded, radius about 55-80 px; see DESIGN.md §9.)

## Layout

    src/hal/              the six hardware interfaces; no board knowledge
    src/boards/amoled18/  this board: pins.h holds every number, board_amoled18.cpp
                          implements the interfaces and detects the revision;
                          es8311/ is Espressif's codec driver (Apache-2.0) as
                          carried by Waveshare's demo
    src/ui/               LVGL glue (lvgl_port), the translator screen, the spectrum
                          widget, theme, and the M0 test screen
    src/brain/            the interaction state machine; owns the conversation
    src/synth/            chord synth: voices, envelopes, limiter, own task on core 0
    src/listen/           mic -> FFT -> bands, voice activity, YIN pitch; own task on core 0
    src/motion/           pick-up, shake, face-down from accelerometer samples
    src/power/            activity, standby/off timers, brightness, power button, light sleep
    src/phrases/          phrase bank, loaded from data/phrases.json on LittleFS
    tools/check_phrases.py validates the phrase file
    src/main.cpp          boot, wiring, serial simulation of mic and IMU events
    include/lv_conf.h     LVGL 9.6 configuration
    boards/               PlatformIO board file (16 MB flash, octal PSRAM)
    data/                 LittleFS image: phrases.json
    vendor/               gitignored; keep Waveshare's demo zip here as the reference

Later boards are an extra `[env:...]` in `platformio.ini` plus a
`src/boards/<name>/` folder.
