# Eridian Translator

A handheld desk toy that speaks in chords and listens back, on the Waveshare
ESP32-S3-Touch-AMOLED-1.8. `rocky-translator-brief.md` is the brief,
`DESIGN.md` the design and build plan. This is the **M0 bring-up** state:
the firmware proves the hardware and nothing more. M0 was tested on a V2 board
on 2026-11-08; see the status note in DESIGN.md §14 for what was verified.

## Build and flash

PlatformIO with the official `espressif32` platform 8.x (Arduino core 3.3).

    pio run -e amoled18 -t upload
    pio device monitor

Close the monitor before flashing. The board does not leave download mode by
itself: power-cycle it (or hold PWR 6 s) after a flash if it sits silent.

## What M0 shows

Serial (USB CDC, 115200): chip, flash and PSRAM sizes (expect 16 MB and
about 8192 KB), the I2C scan with device names, the detected board revision
(V1 SH8601 + FT3168, or V2 CO5300 + CST820), then one status line per second
with accelerometer, battery and power-button state.

Screen: four colour bars (red, green, blue, white), a 1 px white frame at the
very edge, a small accent rectangle at odd coordinates (tests the even-pixel
rule), the firmware version and revision text, the live status block, and an
amber dot that follows a finger.

Serial commands: `b 50` brightness percent, `p 0` / `p 1` panel off/on,
`a 0` / `a 1` amp enable (GPIO 46), `s` rescan I2C.

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
                          implements the interfaces and detects the revision
    src/ui/               LVGL glue (lvgl_port) and the M0 test screen
    src/main.cpp          bring-up sketch
    include/lv_conf.h     LVGL 9.6 configuration
    boards/               PlatformIO board file (16 MB flash, octal PSRAM)
    data/                 LittleFS image (phrases.json from M3)
    vendor/               gitignored; keep Waveshare's demo zip here as the reference

Later boards are an extra `[env:...]` in `platformio.ini` plus a
`src/boards/<name>/` folder. Audio is a stub until M1 vendors the ES8311
driver from Waveshare's Arduino demo.
