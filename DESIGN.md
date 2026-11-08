# Eridian Translator: design and build plan

Draft written 2026-11-07, the day before the board arrives. Everything marked
**verified** was checked against the installed toolchain or a primary source;
everything marked **assumed** needs the real board or Waveshare's demo code to
confirm. The brief is `rocky-translator-brief.md`; this document says how to
build it.

## 1. What it does, in one screen

A handheld console that speaks in chords and listens back.

```
 ┌──────────────────────────────┐
 │ ● LISTENING            ▮▮▮ 82%│  status strip (thin)
 ├──────────────────────────────┤
 │                              │
 │   ▁▂▃▅▇▅▃▂▁▂▄▆▇▆▄▂▁▁▂▃▂▁    │  spectrum, 24 glowing bars
 │                              │  (real FFT when listening,
 │                              │   synth peaks when speaking)
 ├──────────────────────────────┤
 │          C4  E4  G4          │  note readout
 ├──────────────────────────────┤
 │  Hello friend. You are       │  translation, typed out,
 │  loud today.▌                │  max 3 lines, cursor blinks
 │                              │
 └──────────────────────────────┘
```

Behaviour is a small state machine (section 8). Everything else is plumbing
to feed it: a synth task, a listening task, an IMU gesture detector, LVGL.

## 2. Toolchain (verified locally)

The planespotter machine already has what this needs. No fork of the
platform is required.

| Item | Installed | Note |
|------|-----------|------|
| PlatformIO Core | 6.2.0 | |
| `platformio/espressif32` | 8.0.0 | official platform, not the pioarduino fork |
| Arduino-ESP32 core | 3.3.12 (package 4.30312) | brief requires >= 3.0.6 |
| ESP-IDF underneath | 5.5.5 | |
| ESP-DSP | 1.8.0 precompiled in the S3 libs (1.8.2 in the esp32 libs) | `dsps_fft2r_fc32` etc. come free, no `lib_deps` entry |
| `esp_lcd` base | present in the core's libs | only an SH8601 vendor driver would need adding, if we go that route |
| `ESP_I2S` library | present | new `I2SClass` API: `setPins(bclk, ws, dout, din, mclk)`, `begin(I2S_MODE_STD, rate, 16BIT, STEREO)`, blocking `read`/`write` |
| LittleFS, Preferences, HTTPUpdate, NetworkClientSecure | present | same set planespotter's OTA uses |
| Bootloader rollback | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1` | so the planespotter OTA port keeps its rollback semantics |

**Smoke test, 2026-11-07:** a throwaway project with the board file below,
`lvgl/lvgl @ ^9.3` (resolved 9.6.0), `bblanchon/ArduinoJson @ ^7` (7.4.3),
an `ESP_I2S` include, a 1024-point `dsps_fft2r_fc32` call and the variant's
pin macros compiled cleanly for the S3. PlatformIO downloaded the S3 libs
package (`framework-arduinoespressif32-esp32s3-libs` 3.3.12, with
`qio_opi` and `opi_opi` memory builds) on that run, so the toolchain is ready
for tomorrow. The empty firmware is 730 KB of a 6.25 MB slot and 98 KB of
internal RAM, so LVGL plus fonts plus the synth have ample room.

### Board definition

The platform has no board JSON for the 1.8" AMOLED, but the Arduino core
ships a pin variant for it: `variants/waveshare_esp32_s3_touch_amoled_18/pins_arduino.h`
(**verified**, see section 3). So the project carries its own board file,
`boards/waveshare_esp32_s3_touch_amoled_18.json`, modelled on the platform's
`waveshare_esp32_s3_touch_amoled_206.json`:

```json
{
  "build": {
    "arduino": { "ldscript": "esp32s3_out.ld", "partitions": "default_16MB.csv",
                 "memory_type": "qio_opi" },
    "core": "esp32",
    "extra_flags": ["-DARDUINO_WAVESHARE_ESP32_S3_TOUCH_AMOLED_18",
                    "-DARDUINO_RUNNING_CORE=1", "-DARDUINO_EVENT_RUNNING_CORE=1",
                    "-DBOARD_HAS_PSRAM", "-DARDUINO_USB_MODE=1",
                    "-DARDUINO_USB_CDC_ON_BOOT=1"],
    "f_cpu": "240000000L", "f_flash": "80000000L", "flash_mode": "qio",
    "mcu": "esp32s3", "variant": "waveshare_esp32_s3_touch_amoled_18"
  },
  "connectivity": ["wifi", "bluetooth"],
  "frameworks": ["arduino"],
  "name": "Waveshare ESP32-S3-Touch-AMOLED-1.8",
  "upload": { "flash_size": "16MB", "maximum_ram_size": 327680,
              "maximum_size": 16777216, "require_upload_port": true, "speed": 921600 },
  "url": "https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm",
  "vendor": "Waveshare"
}
```

`qio_opi` means quad flash plus octal PSRAM, which is what the S3R8 has.
Getting this wrong is the classic "boots, then crashes the moment PSRAM is
touched" failure, so M0 starts by printing `ESP.getPsramSize()` and expecting
about 8 MB.

### Partitions

Use the core's `default_16MB.csv` (**verified** contents):

| Partition | Size | Use |
|-----------|------|-----|
| app0 / app1 | 6.25 MB each | two OTA slots; LVGL + synth firmware will be 1.5 to 2.5 MB, plenty of room |
| spiffs | 3.4 MB | mounted as LittleFS (the LittleFS library uses the partition labelled `spiffs` by default); phrases, future sprites, fonts |
| coredump | 64 KB | keep |

As with planespotter: the table cannot change over the air, so this is
decided before the first unit is handed over.

### platformio.ini sketch

```ini
[platformio]
default_envs = amoled18
boards_dir = boards

[env]
platform = espressif32 @ ^8.0.0
framework = arduino
monitor_speed = 115200
build_flags =
    -DLV_CONF_INCLUDE_SIMPLE
    -Iinclude                       ; lv_conf.h lives here
    -DCORE_DEBUG_LEVEL=2
lib_deps =
    lvgl/lvgl @ ^9.3                ; pin exact once it builds
    bblanchon/ArduinoJson @ ^7

[env:amoled18]
board = waveshare_esp32_s3_touch_amoled_18
board_build.filesystem = littlefs
build_flags = ${env.build_flags} -DBOARD_AMOLED18=1 -DBOARD_VARIANT=\"amoled18\"
lib_deps = ${env.lib_deps}
    moononournation/GFX Library for Arduino @ ^1.6.9   ; Arduino_ESP32QSPI + Arduino_CO5300 / Arduino_SH8601
    adafruit/Adafruit XCA9554 @ ^1.0                   ; TCA9554 expander, what Waveshare's demos use
    lewisxhe/XPowersLib @ ^0.3                         ; AXP2101
    lewisxhe/SensorLib @ ^0.5                          ; QMI8658 (wake-on-motion), PCF85063
    ; ES8311: vendored es8311.c/.h from Waveshare's Arduino demo in src/boards/amoled18/
    ; touch: two ~40-line register readers of our own (CST820 and FT3168), see section 3
```

Library notes (**verified** on 2026-11-07 from the libraries' sources and
release pages):

- Arduino_GFX 1.6.9 is current; its LVGL 9 example is the flush pattern to
  copy. It does not align flush areas, so LVGL must (section 9).
- SensorLib 0.5.1 does the IMU and RTC but its FocalTech touch driver does
  not accept the FT3168's chip ID, and it has no CST820 support. Waveshare's
  `Arduino_DriveBus` does both but is GPL-3. Both chips expose the same
  trivial register layout (finger count at 0x02, coordinates at 0x03..0x06),
  so two small readers of our own are the clean answer and keep the repo
  MIT-licensable.
- ES8311: Waveshare's Arduino demo carries Espressif's `es8311.c/.h`
  ported to the Arduino I2C HAL, configured for this board (analog mic,
  MCLK 256 × fs). Vendoring that is the proven path. The maintained
  alternative is `pschatzmann/arduino-audio-driver` 0.3.2 (ES8311 with
  custom pins, ADC and DAC together); switch to it if the vendored file
  becomes a maintenance burden.
- Every I2C library above uses `Wire`. Do not add anything built on IDF's
  new `i2c_master` driver (ESP32_IO_Expander, esp_lcd_touch components):
  core 3.3 aborts at boot when both drivers are present.

Later boards (the CYD with an INMP441, a 3.5" S3) are extra `[env:...]`
blocks with their own `-DBOARD_...` flag and `src/boards/<name>/` folder.

## 3. Board facts (from Waveshare's schematic, demo code and BSP; details in Appendix A)

**Two hardware revisions exist.** V1 has a SH8601 panel and FT3168 touch;
V2, shipping since 2026-05-30, has a **CO5300 panel and CST820 touch** with
a 16-pixel column offset. A board bought now is almost certainly V2. The pin
map is identical, so the firmware probes the touch controller at boot
(I2C 0x15 = CST820, so V2; else 0x38 = FT3168, so V1) and picks the panel
class and the column offset from that. Both drivers are compiled in. The
brief's FT6146 appears only in the product-page spec table and in no source
code anywhere; treat it as a label for FT3168.

| Function | GPIO / route | Note |
|----------|--------------|------|
| AMOLED QSPI CS, SCLK | 12, 11 | 40 MHz in every demo |
| AMOLED QSPI D0..D3 | 4, 5, 6, 7 | |
| AMOLED TE | 13 | wired, unused by every demo |
| AMOLED reset | expander EXIO0 | no GPIO; must be pulsed before the SPI bus is set up |
| AMOLED power enable | expander EXIO1 | |
| Touch INT | 21 | active low; the only interrupt on a real GPIO |
| Touch reset | expander EXIO2 | |
| I2C SDA / SCL | **15 / 14** | Confirmed on the board 2026-11-08. The Arduino core variant file says the opposite; never rely on its `SDA`/`SCL` constants |
| I2S MCLK, BCLK, WS | 16, 9, 45 | MCLK = 256 × fs in the Arduino demo |
| I2S DOUT (to codec DAC), DIN (from codec ADC) | 8, 10 | the demo's legacy macros have these swapped; schematic settles it |
| Speaker amp enable (NS4150B) | **46**, active high | a strapping pin; one user reports it would not drive high under PlatformIO, so verify with a meter on day 1 |
| TF card | SDMMC 1-bit: CLK 2, CMD 1, D0 3; CS via EXIO7 (hold high) | not needed for this project |
| Power button | AXP2101 PWRON, and state readable on EXIO4 | hold ≥ 6 s to power on/off (PMIC default) |
| Battery sense | AXP2101 ADC only | no ESP32 ADC pin |
| BOOT button | 0 | |
| Free pads | 17, 18, 38, 39, 40, 41, 42 | plus UART0 43/44 |

I2C addresses: TCA9554 expander 0x20, FT3168 0x38, CST820 0x15, AXP2101
0x34, ES8311 0x18, QMI8658 0x6B, PCF85063 0x51.

TCA9554 expander (0x20) pin roles: EXIO0 LCD reset, EXIO1 display power
enable, EXIO2 touch reset, EXIO3 RTC interrupt, EXIO4 power-button state,
EXIO5 **AXP2101 IRQ**, EXIO6 **QMI8658 INT1**, EXIO7 SD chip select. The
IMU and PMIC interrupts therefore are not on GPIOs and cannot wake the chip
from light sleep by themselves; see section 11. Whether the expander's own
INT output reaches an ESP32 pin is not legible in the schematic text; check
on day 1, because it would restore interrupt-driven wake.

Power: the AXP2101's defaults have every rail on at boot and none of the
current demos touch them. The factory firmware turns everything off except
DC1 (3.3 V system) and ALDO1 (3.3 V, mic/analog audio), and the panel,
touch and codec still work, so the panel runs from the 3.3 V system rail
gated by EXIO1. Our policy: leave rails at defaults, explicitly enable
ALDO1, and use `shutdown()` for power-off. Do not copy the legacy
`01_AXP2101` IDF demo's voltages; they contradict the schematic.

## 4. Repository layout and module boundaries

```
rocky-translator/
  platformio.ini
  boards/waveshare_esp32_s3_touch_amoled_18.json
  partitions/                      (only if we leave default_16MB.csv)
  include/lv_conf.h
  data/phrases.json                -> LittleFS image (pio run -t uploadfs)
  src/
    main.cpp                       boot, wire modules, run the loop; small
    hal/                           pure interfaces, no board code
      display.h  touch.h  audio_out.h  audio_in.h  motion.h  power.h
    boards/amoled18/
      board_amoled18.h/.cpp        implements all six interfaces for this board
      pins.h                       every pin and I2C address, nothing else has numbers
    synth/      voice.h/.cpp  wavetable.h  envelope.h  synth_engine.h/.cpp
    listen/     fft_bands.h/.cpp  vad.h/.cpp  pitch_yin.h/.cpp  notes.h
    ui/         ui.h/.cpp  spectrum_widget.cpp  typewriter.cpp  theme.h  burnin.cpp
    phrases/    phrase_bank.h/.cpp
    brain/      brain.h/.cpp       the state machine; owns the conversation
    motion/     gestures.h/.cpp    pick-up / shake / face-down from raw IMU samples
    settings/   settings.h/.cpp    NVS-backed struct
    ota/        ota.h/.cpp  certs.h   ported from planespotter
  tools/        gen_wavetables.py (optional), check_phrases.py
```

Rules that keep the brief's "separate modules" promise:

- Only `src/boards/**` includes vendor libraries (display driver, XPowersLib,
  SensorLib, codec driver). Everything above talks to the six `hal/` interfaces.
- `synth/` and `listen/` are plain C++ over sample buffers; they do not know
  about I2S. That makes them unit-testable on the host later (`pio test -e native`).
- `ui/` only knows LVGL and a `UiModel` struct the brain fills in (status
  word, 24 band levels, note text, translation text, battery). No audio
  code in the UI, no LVGL calls outside `ui/`.
- `brain/` is the only module that knows the interaction rules.

## 5. Tasks, cores and queues

| Task | Core | Priority | Period | Job |
|------|------|----------|--------|-----|
| `synth` | 0 | high (e.g. 20) | driven by I2S DMA back-pressure | render 256-frame blocks, write to AudioOut |
| `listen` | 0 | medium (10) | ~30 Hz | read mic block, FFT, VAD, pitch; post events + bands |
| Arduino `loop()` | 1 | default | ~5 ms | `lv_timer_handler()`, brain tick, motion poll, touch, power poll |
| `ota` (M6) | 1 | low | on demand | runs the planespotter flow; UI shows progress |

The brief asks for the synth "pinned away from the UI core". Arduino's
`loop()` and the WiFi/event task run on core 1 (`ARDUINO_RUNNING_CORE=1`), so
audio goes on core 0. Core 0 also hosts the WiFi driver's lower half; audio
has priority over it and only matters during OTA anyway.

Communication:

- `ChordQueue` (FreeRTOS queue, depth 16) brain -> synth. Item: up to 4 MIDI
  notes, duration ms, velocity, gap ms.
- `EventQueue` (depth 16) everything -> brain. Variants: `Tap`, `LongPress`,
  `PickedUp`, `Shaken`, `FaceDown`, `FaceUp`, `SpeechStart`, `SpeechEnd`,
  `WhistleDetected{midi, cents}`, `ChordStarted{notes}`, `PhraseDone`,
  `PowerButtonShort`, `PowerButtonLong`, `IdleTimeout`.
- `Bands` (24 floats, double-buffered, one `portMUX` around the swap)
  listen/synth -> UI. Whoever is "driving the display" (listen when
  listening, synth when speaking) writes it; the UI reads once per frame.
- `SynthState` (atomic flags): `playing`, `lastNoteOffMs`, read by listen to
  gate the mic.

## 6. Synth engine

### Voices

- 4 voices, each: 32-bit phase accumulator, wavetable lookup with linear
  interpolation, ADSR, per-voice detune, shared vibrato LFO.
- Wavetables: 1024 samples, `int16`, built at boot (no Python step needed):
  - `sine`
  - `eridian` = 0.65 sine + 0.25 triangle + 0.10 third harmonic, then
    normalised. Slightly reedy, reads as "organ heard through water" on a tiny
    speaker. Make the blend weights settings so the sound can be tuned on the
    real speaker.
- Detune: voices 1..3 offset by -4, +3, +6 cents; vibrato 5.5 Hz, depth 6 cents,
  fading in over the first 150 ms so chord onsets stay clean.
- ADSR defaults: A 12 ms, D 80 ms, S 0.8, R 120 ms. The release runs past the
  chord's nominal duration so successive chords overlap a little (legato);
  the brain schedules the next chord at `ms`, not at `ms + release`.
- Chord voicing: notes come from the phrase file as MIDI. Clamp into C4–C6
  by octave shifting before rendering (the speaker rule from the brief), and
  expose `bassCut` as a one-pole high-pass around 220 Hz on the mix.

### Mix and output

- Render in 256-frame blocks (11.6 ms at 22.05 kHz) into a float or Q15
  buffer, sum voices, apply the soft limiter
  `y = x / (1 + |x|)` scaled so a 4-voice fortissimo peaks around -1 dBFS,
  then master volume, then duplicate to L and R `int16` and `write()`.
- Keep I2S running always, writing zeros when idle. Starting and stopping
  the peripheral is where clicks come from. The NS4150B amplifier enable
  (GPIO 46) is switched on once at boot after the codec is configured and
  the DAC is at zero, with a short ramp of the codec volume. GPIO 46 is a
  strapping pin: configure it as an output only after boot, and confirm it
  reaches 3.3 V with a meter (one report says it would not under PlatformIO).
- Codec: vendor the `es8311.c/.h` that Waveshare's Arduino demo carries
  (Espressif's driver ported to the Arduino I2C HAL) behind `AudioOut` and
  `AudioIn`; it already does DAC and analog-mic ADC on the one chip, MCLK
  from GPIO 16. The V2 speaker is noticeably quieter than V1 (Waveshare
  raised their demo volume from 70 to 90), so the default volume setting
  should be revision-aware.
- Clip and pop checks for M1: a scope is nice but ears are enough. Play a
  single C4 for 2 s, then a 4-note chord; nothing should tick at onset or
  release, and the LVGL redraws of the spectrum must not stutter it.

### Sample rate: one for both directions

The brief says 22.05 kHz out and 16 kHz in. The ES8311 is one codec on one
I2S bus with one MCLK/BCLK/LRCK, so the ADC and DAC run at the same rate.
**Decision: run everything at 22.05 kHz.** The listener gets 21.5 Hz FFT bins
with a 1024-point FFT, which is fine for 24 display bands and for whistle
pitch (whistles live at 500 to 3000 Hz). YIN pitch tracking is unaffected.
If the codec's MCLK ratios make 22.05 kHz awkward, 24 kHz is the fallback;
nothing in the design depends on the exact number, only on `SAMPLE_RATE`.

## 7. Listening engine

Runs in the `listen` task on core 0.

1. Read 1024 mono samples from AudioIn (the ES8311 ADC path; take the left
   slot). Hop of 735 samples gives 30 frames per second at 22.05 kHz; keep a
   ring so the FFT window overlaps.
2. **Gate**: if `SynthState.playing` or `millis() - lastNoteOffMs < 300`,
   drop the block (still update the noise floor slowly). This is the
   "don't let it hear itself" rule. Belt and braces: also reduce ES8311 mic
   gain while speaking if the codec supports fast gain changes.
3. Window (Hann), `dsps_fft2r_fc32` from ESP-DSP, magnitude.
4. **Bands**: 24 log-spaced bands from 80 Hz to 8 kHz, dB scale, fast attack
   and slow release (visual smoothing), normalised against a running peak so
   the display fills nicely in quiet and loud rooms alike. Published to
   `Bands`.
5. **Voice activity**: energy in 150 Hz to 4 kHz. Noise floor is an
   exponential moving average that only updates while below threshold
   (time constant ~5 s). Threshold = floor + `micSensitivity` dB (setting,
   default 12 dB). Speech starts after 300 ms above, ends after 700 ms below.
6. **Whistle vs speech** (decided at `SpeechEnd`, or earlier once 400 ms of
   stable pitch is seen): a whistle or hum has one dominant peak with high
   peak-to-mean ratio (spectral flatness low) and a pitch track whose
   frame-to-frame change stays under 50 cents for 400 ms. Speech has a
   wandering pitch and energy spread. Emit `WhistleDetected{midi, cents}` or
   `SpeechStart/SpeechEnd`.
7. **Pitch**: YIN on the 1024-sample window (about 100 lines, hand-written;
   no good Arduino library exists). Search 80 Hz to 3 kHz, threshold 0.15,
   parabolic interpolation. Convert to MIDI with `69 + 12*log2(f/440)`, keep
   the cents offset for the mirror reply.

CPU estimate: a 1024-point float FFT on the S3 with ESP-DSP is well under 1
ms; YIN at this window size is a few ms. 30 Hz is comfortable on one core
alongside the synth.

ESP-DSP availability: the S3 libs package that PlatformIO installed carries
`libespressif__esp-dsp.a` (1.8.0) and its headers, and the smoke test
linked `dsps_fft2r_fc32` without any `lib_deps` entry (**verified**). Some
online sources claim the Arduino core does not bundle ESP-DSP; that was true
of older cores, not of 3.3.12. If a future core drops it, vendor
`espressif/esp-dsp` under `lib/` with a `library.json` that builds only
`modules/**`, or fall back to `kosme/arduinoFFT`.

## 8. Brain: the state machine

```
            boot
             │
             ▼
   ┌─────► IDLE ◄────────────────────────────┐
   │        │ 60 s no events                  │ touch / sound / pick-up
   │        ▼                                 │
   │     STANDBY (dim, drifting dot) ─────────┤
   │        │ 5 min                           │
   │        ▼                                 │
   │      OFF (panel off, light sleep) ───────┘
   │
   │  IDLE/LISTENING on:  Tap ─────────────► SPEAKING(next in exchange)
   │                      SpeechStart ─────► RECEIVING
   │                      WhistleDetected ─► NOTE_SHOWN ─► SPEAKING(reply or mirror)
   │                      PickedUp ────────► SPEAKING(greeting)
   │                      Shaken ──────────► SPEAKING(alarmed)
   │                      FaceDown ────────► OFF
   │                      LongPress ───────► SETTINGS
   │
   │  RECEIVING: spectrum = mic; on SpeechEnd ─► SPEAKING(reply)
   │  SPEAKING:  chords queued, spectrum = synth peaks + noise, notes shown
   │             on PhraseDone ─► TRANSLATING
   │  TRANSLATING: text types out (~30 chars/s), cursor blinks
   │             when done ─► LISTENING (status word) ≡ IDLE with mic on
   └── SETTINGS: LVGL list; Back or 20 s timeout ─► IDLE
```

Notes:

- "IDLE" and "LISTENING" are the same state; the status word says LISTENING
  when the mic is enabled in settings and IDLE otherwise.
- The brain owns an `exchange` cursor: a tap plays the next phrase of the
  current exchange; after the last one, or 30 s of silence, the next tap
  starts a fresh exchange.
- Idle chatter (setting, default off): in IDLE, every 3 to 8 minutes, play an
  `idle` phrase. Off on battery below 20 %.
- Every transition sets a `UiModel` and the UI redraws from that. The brain
  never touches LVGL.

## 9. Translator UI (LVGL 9)

Layout at 368 x 448 portrait, in pixels:

| Region | y range | Content |
|--------|---------|---------|
| status strip | 6–34 | 8 px status dot, status word (Montserrat 14), battery glyph + % right-aligned. The bezel corners are rounded with a radius around 55 to 80 px, so the strip's content runs from x = 72 to x = 296 and the strip itself starts 6 px down; the spectrum below is unaffected |
| spectrum | 36–236 | 24 bars, 10 px wide, 4 px gap, rounded tops, glow = a second, wider, dimmer bar behind each |
| note line | 248–288 | Montserrat 28, centred, accent colour |
| translation | 300–430 | Montserrat 24 or 26, 3 lines max, left-aligned with 20 px margins, block cursor as a separate label |
| burn-in margin | all | the whole tree lives in a container inset 6 px so it can shift ±4 px; the translation text keeps a 24 px side margin, which also clears the bottom corners |

Palette: background `#000000`, accent one colour (start with a cyan-green
`#39E5B6`; the "one accent" rule is for the AMOLED power budget and the look).
Secondary text at 55 % of accent, status dot red when a fetch or codec error
is active, otherwise accent.

Rendering decisions:

- Driver: **Arduino_GFX** (`Arduino_ESP32QSPI` bus at 40 MHz on pins
  12/11/4/5/6/7, panel class `Arduino_CO5300` with column offset 16 for V2
  or `Arduino_SH8601` for V1, chosen at runtime from the touch probe). This
  is what Waveshare's own Arduino demos use. Before `begin()`: pulse the
  expander's EXIO0/EXIO1/EXIO2 low for 20 ms then high, or the panel stays
  black (the exact bug in their BSP issue #12). Brightness is register 0x51
  via `setBrightness()`. The panel does no rotation; portrait 368 x 448 is
  native, which is what the layout wants.
- Colour depth RGB565 with swapped bytes (`LV_COLOR_16_SWAP` / LVGL 9's
  `lv_draw_sw_rgb565_swap` in the flush callback, then
  `draw16bitBeRGBBitmap`).
- Draw buffers: two partial buffers of 368 x 40 x 2 bytes = 29 KB each in
  internal RAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA`),
  `LV_DISPLAY_RENDER_MODE_PARTIAL`. A full frame in PSRAM is **not** an
  option with Arduino_GFX: its QSPI bus hands the caller's pointer straight
  to the SPI DMA, and PSRAM is not DMA-capable on that path. If partial
  flushes tear visibly, the remedies are a taller partial buffer (368 x 112
  is 82 KB, still fine) and, as a last resort, the TE line on GPIO 13 to
  time flushes to the panel's refresh.
- **Even-pixel alignment**: SH8601 needs flush areas with even x1/y1 and odd
  x2/y2. Register an `LV_EVENT_INVALIDATE_AREA` callback on the display that
  rounds the area outward. Without it, the panel shows a column of garbage
  on odd-aligned updates.
- Spectrum as a custom-drawn object (one `lv_obj` with a draw event that
  issues 48 rounded rects) rather than 24 `lv_bar`s: one invalidation per
  frame, far fewer objects.
- Typewriter: an `lv_timer` at 33 ms appends one character per tick to the
  label text; the cursor label is repositioned to the label's last glyph
  with `lv_label_get_letter_pos`. Long phrases: once 3 lines are full, scroll
  the label up one line (`lv_obj_scroll_by`) before continuing.
- Frame rate target: 30 fps while the spectrum moves; the spectrum is the
  only thing invalidating per frame.
- `lv_tick_set_cb(millis)` and `lv_timer_handler()` from `loop()`; no
  separate LVGL task, so no LVGL locking is needed as long as only `loop()`
  touches LVGL (the design guarantees this).

Burn-in protection (required by the brief):

- Shift the root container by a pseudo-random offset in [-4, 4] px every
  3 minutes.
- STANDBY after 60 s: brightness to ~15 %, UI hidden, a single 6 px dot
  drifting in a slow Lissajous path. Nothing bright, nothing static.
- OFF after 5 min: brightness 0, display sleep-in command, panel power rail
  off if the PMIC lets us, then light sleep (section 11).
- Global brightness cap in settings; the AMOLED is very bright at 100 %.

## 10. Phrase bank

`data/phrases.json`, loaded once at boot into PSRAM structs via ArduinoJson 7:

```json
{
  "version": 1,
  "phrases": [
    { "id": "greet1", "mood": "warm", "trigger": "picked_up",
      "chords": [ {"notes":[60,64,67], "ms":300}, {"notes":[62,65,69], "ms":450} ],
      "text": "Hello friend." },
    { "id": "q1", "mood": "curious", "trigger": "heard_speech", "next": "q1b",
      "chords": [ {"notes":[67,71,74,79], "ms":600} ],
      "text": "You are loud today. Question?" }
  ]
}
```

- `trigger` in `tap | heard_speech | heard_whistle | picked_up | shaken | idle`.
- Optional `next` chains an exchange for taps; optional `velocity` and
  `gap_ms` per chord.
- Selection: filter by trigger, exclude the last two played, pick uniformly.
- **Mirror reply** for whistles: build a major triad on the whistled note
  (root, +4, +7, optionally +12), play it twice with a small pause, text
  "You said: C5". Enabled by setting.
- Ten placeholder phrases of original text ship in the repo so it runs; the
  file is yours to rewrite. `tools/check_phrases.py` validates the JSON and
  warns about notes outside C4–C6 (they still play, clamped).
- New phrases need `pio run -t uploadfs`, no code change. Later, OTA could
  ship a new `phrases.json` as a release asset the same way as firmware.

## 11. Motion and power

Motion (QMI8658 over I2C, polled at 50 Hz from `loop()`, with the INT1 pin
as a wake source):

- **Pick-up**: the device has been still (|a| within 1.0 g ± 0.05 for 2 s)
  and then |a| deviates by more than 0.25 g, or the orientation vector turns
  by more than 25 degrees within 500 ms. Debounce 2 s.
- **Shake**: high-passed acceleration crosses ±1.5 g with at least 3 sign
  changes in 800 ms.
- **Face-down**: measured on the board, z reads -1.0 g when lying screen-up,
  so face-down is z above +0.8 g for 1 s. **Face-up** clears it.
- Enable the IMU's wake-on-motion before sleeping (SensorLib 0.5:
  `configWakeOnMotion(threshold, ACC_ODR_LOWPOWER_128Hz, INTERRUPT_PIN_1, ...)`),
  so INT1 asserts on a pick-up. It lands on the expander (EXIO6), so it is
  read by polling unless the expander INT turns out to be wired (section 3).
- `esp_light_sleep_start()` refuses to run from a task whose stack is in
  PSRAM; keep the sleep call in `loop()` (internal stack) and never give the
  synth or listen tasks PSRAM stacks.

Power (AXP2101 over I2C):

- Battery % and charging flag polled every 5 s into the `UiModel`.
- Power button: the AXP2101 raises an IRQ on short and long press (IRQ line
  on EXIO5; the button's level is also readable on EXIO4). Poll the expander
  from `loop()`, read the PMIC's IRQ status, map short to sleep/wake and
  long to power-off (`shutdown()`). Note the PMIC itself needs a 6 s hold
  to power on from off.
- On battery: brightness cap 60 %, STANDBY at 30 s, OFF at 2 min, idle
  chatter off below 20 %.
- Sleep: the only interrupt on a real GPIO is touch (21). IMU INT1 and the
  PMIC IRQ arrive on the I/O expander, so from light sleep they can wake us
  only if the expander's INT line turns out to be wired to a GPIO (day-1
  check). Plan A (no expander INT): light sleep with GPIO wake on touch and
  a 250 ms timer wake that reads one IMU sample and the expander's input
  register (a few hundred microseconds awake), then sleeps again; pick-up
  and power button still work, at a few mA average. Plan B (expander INT
  wired): add it as a wake source and drop the timer to 10 s. Waking on
  sound from light sleep is not realistic without the ULP coprocessor, so
  "wake on sound" works only from STANDBY (screen dim, CPU awake), not from
  OFF. The brief allows this outcome; document it in the README.
- I2S and LVGL across light sleep: stop the synth and listen tasks, drain
  I2S, `i2s.end()` before sleep and re-`begin()` after; LVGL state survives
  because RAM is retained.

## 12. Settings

NVS namespace `rocky`, one struct with a version byte:
`volume (0–100)`, `bassCut (bool)`, `micSensitivityDb (6–24)`,
`listening (bool)`, `idleChatter (bool)`, `mirrorReply (bool)`,
`brightnessCap (20–100)`, `wavetableBlend (0–100)`.

Settings screen is an LVGL list opened by long press; each row is a label
plus a slider or switch. Changes apply live and save on exit.

## 13. OTA (M6)

Port `src/ota.h/.cpp` and `certs.h` from planespotter almost unchanged
(**verified** the module has no dependency on the display or the plane
code: its `Ui` struct is two callbacks). Changes:

- `BOARD_VARIANT` is `amoled18`; the manifest grows a key per board later.
- Repo URL and manifest URL point at this project's GitHub repository.
- `markValid()` conditions: 60 s of running, or the first completed phrase,
  or opening the settings screen. Same reasoning as planespotter's
  "first fetch or 60 s or portal".
- Manual check lives in the settings screen ("Check for updates") instead
  of a BOOT press.
- WiFi provisioning: planespotter uses WiFiManager's captive portal. Here a
  touch screen could host its own SSID picker, but the portal is cheaper and
  known to work; reuse it, triggered from settings. WiFi stays off except
  during a check so the audio core is undisturbed.
- Pinned roots, pre-release handling, the rolled-back-version memory and the
  release workflow (`release.yml`, `make_manifest.py`) copy over as-is.

## 14. Milestones, with what to test on the real board

**M0 bring-up (day 1).** Custom board JSON builds; serial shows PSRAM ≈ 8 MB
and flash 16 MB. I2C scan (try SDA 15/SCL 14 first, then the reverse) lists
the expander 0x20, touch (0x15 or 0x38), AXP2101 0x34, ES8311 0x18, QMI8658
0x6B and PCF85063 0x51; the log says "revision V2 (CO5300 + CST820)" or
"V1 (SH8601 + FT3168)". Expander reset pulse, then the display shows a test
pattern with correct colours, no 16 px offset error, no garbage column
(alignment). Touch coordinates land under the finger. IMU prints x/y/z;
PMIC prints battery voltage and charging state; GPIO 46 measures 3.3 V when
driven high. Commit: "M0: bring-up".

Status 2026-11-08, first flash on the real board: **revision V2** (CO5300 +
CST820, touch id 0xB7). PSRAM 8192 KB, flash 16 MB. I2C answers on **SDA 15 /
SCL 14** (Waveshare's order; the Arduino variant file is wrong) with all six
devices present: 0x15 CST820, 0x18 ES8311, 0x20 TCA9554, 0x34 AXP2101,
0x51 PCF85063, 0x6B QMI8658. Display, touch, IMU and PMIC all initialise;
the IMU reads z = -1.0 g lying screen-up. Confirmed by eye: colour order
RGB correct, odd-aligned rectangle clean (rounder works), the panel's straight
edges are fully visible but the **bezel corners are rounded**: guide dots at
8 and 16 px along the corner diagonal are hidden, 24 px is visible, which
puts the corner radius at roughly 55 to 80 px (5 to 6 mm). Layout rule from
that: nothing that must be read sits within 72 px of a side in the top or
bottom 30 px of the screen, and the status strip's dot and battery glyph are
inset accordingly (section 9). Touch: the CST820 must have auto-sleep disabled (reg 0xFE = 1)
and be read only while its INT line is low; polled blindly it lagged by
seconds. USB CDC writes must not block when no host reads
(`Serial.setTxTimeoutMs(0)`), or every print stalls the loop for 100 ms.
Touch lands slightly low; a y offset may be needed. GPIO 46 not yet metered.
Audio is a stub until M1.

Day-1 checklist before any of that, with the board in hand:

1. Note the production batch markings; photograph the PCB.
2. Download Waveshare's demo zip for this board and keep it in `vendor/`
   (gitignored) as the reference for pins and init sequences.
3. Flash a Waveshare Arduino demo first to prove the hardware and cable.
4. Then our M0 skeleton.

**M1 sound.** Tap plays a 3-note chord, no clicks; volume slider works; the
spectrum redraws during playback without audio glitches (this is the
test of the core split and I2S buffering).

Status 2026-11-08, built late in the evening so **not yet listened to**:
`src/synth/synth_engine.cpp` (own task on core 0, priority 20, 4 wavetable
voices, ADSR, detune, vibrato fade-in, 220 Hz bass cut, soft limiter, 256-frame
blocks paced by the I2S DMA) writes to `hal::AudioOut`, implemented in the
board layer with `ESP_I2S` in full duplex and the vendored Espressif
`es8311.c` from Waveshare's demo (MCLK 256 x fs on GPIO 16, 16-bit, analog
mic path configured, mic gain 18 dB). The brain is the conductor: `noteOn`
at each chord start, `noteOff` at its end and in gaps. Verified over serial:
codec initialises at 22.05 kHz, the task renders at the DMA rate, a tap gives
a chord with a pre-limiter peak around 0.4, output stays zero while muted.
**Mute** is a short press of the BOOT button (and `mute` over serial): muted
by default on first boot, remembered in NVS, shown as a mute symbol in the
status strip; muting also drops the amplifier enable so there is no idle
hiss. Volume (`vol`) and bass cut are saved too. Still to do with ears:
click check, loudness, the wavetable blend, and whether 85 % is the right
default on the quieter V2 speaker.

**M2 translator screen.** Full UI; spectrum follows synth; typewriter after
playback; layout photographed at arm's length.

Status 2026-11-08: built and running on the board. `src/ui/translator_screen.cpp`
(status strip with the corner insets, custom-drawn 24-bar spectrum with glow,
note line, typewriter with blinking cursor and 3-line scroll, burn-in shift
every 3 min), `src/brain/brain.cpp` (the §8 state machine: listening,
receiving, note shown, speaking, translating; exchanges via `next`; whistle
mirror reply) and `src/phrases/phrase_bank.cpp` (ten built-in placeholder
phrases, the JSON loader is M3). Chords are timed by a stub in the brain that
also produces the synth-driven spectrum peaks and note names; M1 replaces the
timing with the real synth and keeps the rest. Mic and IMU events are
simulated over serial (`tap`, `speech`, `whistle 72`, `pickup`, `shake`).
Verified over serial: every state transition fires in order. Layout still to
be judged by eye.

**M3 phrases.** `phrases.json` on LittleFS; exchange cycles; editing the JSON
and re-uploading the filesystem changes behaviour with no code change.

Status 2026-11-08: done and verified. `data/phrases.json` holds the phrases
(schema as in §10, plus optional `gap_ms` and `velocity` per chord);
`src/phrases/phrase_bank.cpp` loads it from LittleFS at boot into
`std::vector<Phrase>`, skipping malformed entries with a serial warning, and
falls back to a built-in set if the file is missing. `tools/check_phrases.py`
validates the file. Acceptance test: an eleventh phrase added to the JSON
only, `pio run -t uploadfs`, then `reload` over serial reported 11 phrases
with the firmware untouched. The brain honours `gap_ms` as silence between
chords.

**M4 listening.** Live mic spectrum; speech vs whistle at ~1 m; reply after
the speaker stops; zero self-triggers over a 10-minute soak with idle
chatter on (the gate works).

**M5 motion and power.** Pick-up, shake, face-down; burn-in shift visible
with a ruler against the bezel; standby and off timers; battery %; power
button short/long.

**M6 polish.** Settings screen, idle chatter, OTA with a tagged pre-release.

## 15. Risks and open questions

| Risk | Mitigation |
|------|------------|
| Board is V1 or V2 and the two differ in panel and touch chip | Probe I2C 0x15 then 0x38 at boot; both panel and both touch drivers compiled in; column offset follows the probe |
| Panel stays black | Known cause: expander reset not pulsed before SPI init (Waveshare BSP issue #12). Pulse EXIO0/1/2 first, always |
| I2C SDA/SCL order disagrees between Waveshare sources and the Arduino variant | Scan both ways on day 1; hard-code in `pins.h` |
| GPIO 46 amp enable is a strapping pin and one user could not drive it | Set as output after boot; measure; if it truly will not drive, it is a hardware question for Waveshare, not firmware |
| Mixing the legacy `Wire` I2C driver with libraries using the new `i2c_master` driver aborts the core ("CONFLICT! driver_ng") | Every I2C user goes through `Wire`: Arduino_GFX, Arduino_DriveBus, Adafruit XCA9554, XPowersLib, SensorLib, the vendored es8311.c. Do not add ESP32_IO_Expander or any esp_lcd_touch component |
| Speaker too quiet or boomy | Voicing clamp to C4–C6, bass cut, wavetable blend as settings; test on day 1 of M1 |
| Mic hears the speaker through the case | Hard gate while playing + 300 ms cooldown; if still triggering, add a 1 s cooldown and raise the threshold right after playback |
| PSRAM memory type wrong in board JSON | First M0 print is `ESP.getPsramSize()` |
| LVGL partial flush tearing on QSPI | Switch to a full frame buffer in PSRAM; keep both paths compiled |
| Light sleep breaks I2S or I2C state | Tear down and re-init around sleep; measure with a USB power meter whether sleep is worth it at all for a desk toy |
| Flash size of LVGL + fonts | 6.25 MB slots; irrelevant |

Open questions for the hardware day are collected in the next section.

---

## Appendix A: board facts from Waveshare's sources (collected 2026-11-07)

Sources read: the board's wiki (docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8),
the GitHub repo `waveshareteam/ESP32-S3-Touch-AMOLED-1.8` (README,
`examples/arduino/libraries/Mylibrary/pin_config.h`, the Arduino sketches,
issues), the ESP-IDF BSP component `waveshare/esp32_s3_touch_amoled_1_8`
2.0.3, the schematic PDF (text-extracted), the factory firmware's board
files in `78/xiaozhi-esp32`, and the legacy wiki demo zip (Arduino 3.3.5 +
IDF 5.3.2).

### Revisions

| | V1 | V2 (since 2026-05-30) |
|-|----|-----|
| Panel controller | SH8601 | CO5300, column offset 16 |
| Touch | FT3168 at 0x38, chip ID reg 0xA0 = 0x03 | CST820 at 0x15 (CST816-compatible), ID reg 0xA7 reads 0xB7 |
| Arduino_GFX class | `Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, 368, 448)` | `Arduino_CO5300(bus, GFX_NOT_DEFINED, 0, 368, 448, 16, 0, 0, 0)` |
| Speaker level | reference | quieter; Waveshare's demo volume 90 vs 70 |

Waveshare's IDF BSP uses the CO5300 driver for both revisions and detects
the revision by probing I2C 0x15 then 0x38. Their Arduino demos have
separate sketch sets per revision with no runtime detection.

### Display init (what the demos send)

Arduino_GFX's SH8601/CO5300 tables: SWRESET, SLPOUT (120 ms), NORON,
INVOFF, PIXFMT 0x55 (RGB565), DISPON, 0x53 = 0x28, 0x51 = 0xD0 (brightness),
0x58 = 0x00. The IDF BSP's CO5300 table: FE 00, C4 80, 3A 55, 35 00 (TE on),
53 20, 51 FF, 63 FF, 2A 0000 016F, 2B 0000 01BF, 11 (100 ms), 29. QSPI at
40 MHz, command bits 32, parameter bits 8, quad mode. RGB element order.
Rotation is not supported by either controller (MADCTL flips only). Every
Waveshare LVGL port installs a rounder forcing x1, y1 even and x2, y2 odd.
Brightness is register 0x51.

Mandatory before the SPI bus is initialised: TCA9554 pins 0, 1, 2 as
outputs, low for 20 ms (factory firmware: high 100 ms, low 300 ms), then
high. The BSP 2.0.3 omits this and shows a black screen on V2 (issue #12).

### Audio

Arduino sketch `15_ES8311`: Espressif's `es8311.c/.h` in the sketch folder,
`ESP_I2S` `I2SClass` at 16 kHz, 16-bit stereo, MCLK 256 × fs on GPIO 16,
codec volume 85, analog mic (`es8311_microphone_config(false)`), mic gain
18 dB, `digitalWrite(46, HIGH)` for the NS4150B amp; the loop echoes the mic
to the speaker, which is a useful first test of the full-duplex path. The
IDF BSP defaults to 22.05 kHz mono 16-bit through `esp_codec_dev` with
`pa_pin = 46`; the factory firmware runs 24 kHz both ways. The on-board mic
is analog into the ES8311 ADC, mono.

### Power management

AXP2101 at 0x34. Rails per the schematic: DC1 3.3 V system (ESP32, panel
connector, RTC, SD, IMU, codec digital, expander, amp), DC2 0.9 V, DC3 1.2 V,
DC4 1.8 V, ALDO1 3.3 V analog audio / mic, ALDO2 3.3 V, ALDO3 3.0 V, ALDO4
1.8 V, BLDO1 1.2 V, BLDO2 2.8 V, CPUSLDO 1.2 V. Factory firmware: only DC1
and ALDO1 enabled, charge voltage 4.1 V, power-key off time 4 s. Battery
voltage comes from the PMIC's ADC; the wiki warns the percentage estimate
is jumpy. About one hour of runtime at full brightness on the supplied
cell. Hold the power button 6 s to power on or off at PMIC defaults.

### Toolchain Waveshare targets

Arduino: wiki says core >= 3.0.6; the repo's CI pins 3.3.11 with FQBN
`esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB`, PSRAM left
disabled, serial over USB CDC. Bundled libraries: GFX Library for Arduino
1.6.4, LVGL 8.4.0, Arduino_DriveBus 1.0.1, Adafruit_XCA9554 1.0.0,
SensorLib 0.3.3, XPowersLib 0.2.6. No official PlatformIO support; one
user's PlatformIO attempt is issue #3 (black screen, unexplained).
ESP-IDF: 5.5.5 and 6.0.2 in CI, with the managed BSP and `esp_lvgl_port`
(LVGL 9 in the v9 demo); the IDF path reaches 200 to 300 fps against
50 to 60 in Arduino, which is irrelevant for a 30 fps spectrum.

### Issues worth knowing (github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8/issues)

- #12 open: V2 black screen in all IDF examples; expander reset never released.
- #13 open: GPIO 46 amp enable would not drive high on a V2 board under PlatformIO.
- #1 closed: core 3.3 abort "i2c: CONFLICT! driver_ng" when legacy `Wire` and the new I2C driver coexist; Waveshare moved to Adafruit_XCA9554.
- #3 open: PlatformIO V1 black screen, user saw nothing at 0x20.
- Wiki FAQ: the board does not leave download mode by itself (power-cycle after flashing); close the serial monitor before flashing.
- Home Assistant forum: on V1 the `ft5x06` touch platform works and `cst816` does not; touch occasionally fails to initialise after a reboot.

### Open questions for the hardware day

1. Which revision arrived (probe result, and a photo of the PCB markings).
2. Is SDA 15 / SCL 14 (Waveshare) or 14 / 15 (Arduino variant)?
3. Does the TCA9554 INT output reach an ESP32 GPIO?
4. Does GPIO 46 drive the amp enable high?
5. Speaker loudness and tone at the C4–C6 voicing; does the bass cut help?
6. Does the mic pick up the speaker through the case at normal volume?
