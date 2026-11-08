// Hardware layer for the Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1 and V2).
//
// Boot order matters here (DESIGN.md §3): I2C up, expander found, reset lines
// pulsed (or the panel stays black), touch probed to tell V1 from V2, PMIC rails
// confirmed. Only then can the display and the other devices begin().
#include <Adafruit_XCA9554.h>
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ESP_I2S.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <SensorQMI8658.hpp>

#include "../../hal/board.h"
#include "es8311/es8311.h"
#include "pins.h"

namespace {

enum class Revision { Unknown, V1, V2 };

Revision g_revision = Revision::Unknown;
int g_sda = -1, g_scl = -1;
Adafruit_XCA9554 g_expander;
bool g_expanderOk = false;

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool i2cReadRegs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

int scanBus(hal::BoardInfo &info) {
  info.i2cFoundCount = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (i2cPresent(a) && info.i2cFoundCount < (int)sizeof(info.i2cFound))
      info.i2cFound[info.i2cFoundCount++] = a;
  }
  return info.i2cFoundCount;
}

// Pulse LCD reset, LCD power and touch reset the way the factory firmware does:
// high 100 ms, low 300 ms, high. Without this the panel never answers.
void pulseResets() {
  for (uint8_t p : {pins::EXIO_LCD_RESET, pins::EXIO_LCD_POWER, pins::EXIO_TP_RESET}) {
    g_expander.pinMode(p, OUTPUT);
    g_expander.digitalWrite(p, HIGH);
  }
  g_expander.pinMode(pins::EXIO_SD_CS, OUTPUT);
  g_expander.digitalWrite(pins::EXIO_SD_CS, HIGH);
  for (uint8_t p : {pins::EXIO_PWR_BUTTON, pins::EXIO_PMIC_IRQ, pins::EXIO_IMU_INT1, pins::EXIO_RTC_INT})
    g_expander.pinMode(p, INPUT);
  delay(100);
  for (uint8_t p : {pins::EXIO_LCD_RESET, pins::EXIO_LCD_POWER, pins::EXIO_TP_RESET})
    g_expander.digitalWrite(p, LOW);
  delay(300);
  for (uint8_t p : {pins::EXIO_LCD_RESET, pins::EXIO_LCD_POWER, pins::EXIO_TP_RESET})
    g_expander.digitalWrite(p, HIGH);
  delay(100);
}

// ---------------------------------------------------------------- Display

class Amoled18Display : public hal::Display {
 public:
  bool begin() override {
    if (panel_) return true;
    bus_ = new Arduino_ESP32QSPI(pins::LCD_CS, pins::LCD_SCLK, pins::LCD_D0, pins::LCD_D1, pins::LCD_D2,
                                 pins::LCD_D3);
    if (g_revision == Revision::V1) {
      panel_ = new Arduino_SH8601(bus_, GFX_NOT_DEFINED, 0, pins::LCD_WIDTH, pins::LCD_HEIGHT);
    } else {
      // V2, and the best guess when the probe found neither touch chip.
      panel_ = new Arduino_CO5300(bus_, GFX_NOT_DEFINED, 0, pins::LCD_WIDTH, pins::LCD_HEIGHT,
                                  pins::LCD_COL_OFFSET_V2, 0, 0, 0);
    }
    if (!panel_->begin(pins::LCD_QSPI_HZ)) {
      Serial.println("display: panel begin() failed");
      return false;
    }
    panel_->fillScreen(0x0000);  // RGB565 black
    setBrightness(80);
    return true;
  }
  int width() const override { return pins::LCD_WIDTH; }
  int height() const override { return pins::LCD_HEIGHT; }
  void flush(int x1, int y1, int x2, int y2, const uint16_t *pixels) override {
    if (!panel_) return;
    panel_->draw16bitBeRGBBitmap(x1, y1, const_cast<uint16_t *>(pixels), x2 - x1 + 1, y2 - y1 + 1);
  }
  void setBrightness(uint8_t percent) override {
    if (!panel_) return;
    if (percent > 100) percent = 100;
    panel_->setBrightness((uint8_t)((percent * 255U) / 100U));
  }
  void setPower(bool on) override {
    if (!panel_) return;
    if (on) panel_->displayOn();
    else panel_->displayOff();
  }

 private:
  Arduino_DataBus *bus_ = nullptr;
  Arduino_OLED *panel_ = nullptr;
};

// ------------------------------------------------------------------ Touch
//
// The CST820's INT line pulses low for each report rather than staying low for
// the whole touch, so a level check at poll time misses reports. An ISR latches
// the falling edge; the poll reads the chip when a report arrived or a finger
// was down last time.
volatile bool g_touchReport = false;
void IRAM_ATTR touchIsr() { g_touchReport = true; }

// Both controllers share the FocalTech-style register layout: finger count,
// then XH (bits 3:0), XL, YH (bits 3:0), YL. Writing our own reader for each
// keeps the repo free of the GPL driver Waveshare bundles.

class Amoled18Touch : public hal::Touch {
 public:
  bool begin() override {
    if (g_revision == Revision::V1 && i2cPresent(pins::ADDR_TOUCH_FT3168)) {
      addr_ = pins::ADDR_TOUCH_FT3168;
      firstReg_ = 0x02;
      uint8_t id = 0;
      i2cReadRegs(addr_, 0xA0, &id, 1);
      snprintf(name_, sizeof name_, "FT3168 (id 0x%02X)", id);
      xOffset_ = 0;
    } else if (i2cPresent(pins::ADDR_TOUCH_CST820)) {
      addr_ = pins::ADDR_TOUCH_CST820;
      firstReg_ = 0x02;
      uint8_t id = 0;
      i2cReadRegs(addr_, 0xA7, &id, 1);
      snprintf(name_, sizeof name_, "CST820 (id 0x%02X)", id);
      xOffset_ = pins::TOUCH_X_OFFSET_V2;
      // CST816-family: stop it auto-sleeping (asleep it ignores I2C until a touch
      // wakes it, which shows up as missed and late touches when polling), and
      // report continuously while touched, not only on change.
      i2cWriteReg(addr_, 0xFE, 0x01);  // DisAutoSleep
      i2cWriteReg(addr_, 0xFA, 0x60);  // IrqCtl: EnTouch | EnChange
      useInt_ = true;
    } else {
      snprintf(name_, sizeof name_, "none");
      return false;
    }
    pinMode(pins::TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pins::TP_INT), touchIsr, FALLING);
    return true;
  }
  hal::TouchPoint read() override {
    hal::TouchPoint p;
    if (!addr_) return p;
    // INT is low while a finger is down. Skip the I2C transaction when it is
    // high and nothing was pressed last time; this is how the datasheet expects
    // the chip to be driven and avoids hammering it between touches.
    if (useInt_ && !g_touchReport && !lastPressed_ && digitalRead(pins::TP_INT) == HIGH) return p;
    g_touchReport = false;
    uint8_t r[5];
    if (!i2cReadRegs(addr_, firstReg_, r, sizeof r)) return p;
    uint8_t fingers = r[0] & 0x0F;
    if (fingers == 0 || fingers > 5) {
      lastPressed_ = false;
      return p;
    }
    lastPressed_ = true;
    int16_t x = (int16_t)(((r[1] & 0x0F) << 8) | r[2]) + xOffset_;
    int16_t y = (int16_t)(((r[3] & 0x0F) << 8) | r[4]);
    if (x < 0) x = 0;
    if (x >= pins::LCD_WIDTH) x = pins::LCD_WIDTH - 1;
    if (y >= pins::LCD_HEIGHT) y = pins::LCD_HEIGHT - 1;
    p.pressed = true;
    p.x = x;
    p.y = y;
    return p;
  }
  const char *chipName() const override { return name_; }

 private:
  uint8_t addr_ = 0;
  uint8_t firstReg_ = 0x02;
  int16_t xOffset_ = 0;
  bool useInt_ = false;
  bool lastPressed_ = false;
  char name_[24] = "none";
};

// ------------------------------------------------------------------ Audio
//
// One I2S peripheral in full duplex to the ES8311 (DAC and analog mic on the
// same chip, so one sample rate for both). The codec driver is Espressif's
// es8311.c as carried by Waveshare's Arduino demo, in ./es8311/. Codec
// configuration follows that demo: MCLK from the MCLK pin at 256 x fs, 16-bit.

I2SClass g_i2s;
bool g_i2sUp = false;
uint32_t g_i2sRate = 0;
es8311_handle_t g_codec = nullptr;

bool i2sBegin(uint32_t rate) {
  if (g_i2sUp) return g_i2sRate == rate;
  g_i2s.setPins(pins::I2S_BCLK, pins::I2S_WS, pins::I2S_DOUT, pins::I2S_DIN, pins::I2S_MCLK);
  if (!g_i2s.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("audio: I2S begin failed");
    return false;
  }
  g_codec = es8311_create(0, ES8311_ADDRRES_0);  // Wire has already set up I2C port 0
  if (!g_codec) {
    Serial.println("audio: ES8311 create failed");
    return false;
  }
  const es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = (int)(rate * 256),
      .sample_frequency = (int)rate,
  };
  if (es8311_init(g_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK ||
      es8311_sample_frequency_config(g_codec, clk.mclk_frequency, clk.sample_frequency) != ESP_OK ||
      es8311_microphone_config(g_codec, false) != ESP_OK) {
    Serial.println("audio: ES8311 init failed (is the 22.05 kHz row in its clock table?)");
    return false;
  }
  // 42 dB: measured 2026-11-08, silence peaks ~350/32767 and the own speaker at volume 60 ~13000,
  // so speech at arm's length sits well clear of the floor without clipping. The demo used 18 dB.
  es8311_microphone_gain_set(g_codec, (es8311_mic_gain_t)7);
  g_i2sUp = true;
  g_i2sRate = rate;
  // Prime the DAC with silence before the amplifier comes on: no turn-on thump.
  static int16_t zeros[512] = {};
  for (int i = 0; i < 8; i++) g_i2s.write((uint8_t *)zeros, sizeof zeros);
  Serial.printf("audio: ES8311 up at %u Hz\n", (unsigned)rate);
  return true;
}

class Amoled18AudioOut : public hal::AudioOut {
 public:
  bool begin(uint32_t sampleRate) override {
    pinMode(pins::AMP_ENABLE, OUTPUT);
    digitalWrite(pins::AMP_ENABLE, LOW);
    return i2sBegin(sampleRate);
  }
  size_t write(const int16_t *stereoFrames, size_t frames) override {
    if (!g_i2sUp) {
      delay(frames * 1000 / 22050);  // keep the synth task paced even without hardware
      return frames;
    }
    return g_i2s.write((uint8_t *)stereoFrames, frames * 4) / 4;
  }
  void setVolume(uint8_t percent) override {
    if (g_codec) es8311_voice_volume_set(g_codec, percent > 100 ? 100 : percent, nullptr);
  }
  void setAmpEnabled(bool on) override { digitalWrite(pins::AMP_ENABLE, on ? HIGH : LOW); }
};

volatile int g_micPeakL = 0, g_micPeakR = 0;  // of the last read, for diagnostics

class Amoled18AudioIn : public hal::AudioIn {
 public:
  bool begin(uint32_t sampleRate) override { return i2sBegin(sampleRate); }
  size_t read(int16_t *monoFrames, size_t frames) override {
    if (!g_i2sUp) return 0;
    static int16_t stereo[1024];
    size_t done = 0;
    while (done < frames) {
      size_t want = frames - done;
      if (want > 512) want = 512;
      size_t got = g_i2s.readBytes((char *)stereo, want * 4) / 4;
      if (!got) break;
      int pl = 0, pr = 0;
      for (size_t i = 0; i < got; i++) {
        monoFrames[done + i] = stereo[2 * i];  // left slot
        int l = abs(stereo[2 * i]), r = abs(stereo[2 * i + 1]);
        if (l > pl) pl = l;
        if (r > pr) pr = r;
      }
      g_micPeakL = pl;
      g_micPeakR = pr;
      done += got;
    }
    return done;
  }
  void setGainDb(int db) override {
    if (!g_codec) return;
    int step = db / 6;  // the ES8311 gain enum is in 6 dB steps, 0..7
    if (step < 0) step = 0;
    if (step > 7) step = 7;
    es8311_microphone_gain_set(g_codec, (es8311_mic_gain_t)step);
  }
};

// ----------------------------------------------------------------- Motion

class Amoled18Motion : public hal::Motion {
 public:
  bool begin() override {
    if (!imu_.begin(Wire, pins::ADDR_IMU, g_sda, g_scl)) {
      Serial.println("motion: QMI8658 not found");
      return false;
    }
    Serial.printf("motion: QMI8658 chip id 0x%02X\n", imu_.getChipID());
    imu_.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_0);
    imu_.enableAccelerometer();
    ok_ = true;
    return true;
  }
  bool read(hal::Accel &out) override {
    if (!ok_ || !imu_.getDataReady()) return false;
    return imu_.getAccelerometer(out.x, out.y, out.z);
  }

 private:
  SensorQMI8658 imu_;
  bool ok_ = false;
};

// ------------------------------------------------------------------ Power

XPowersPMU *g_pmu = nullptr;

class Amoled18Power : public hal::Power {
 public:
  bool begin() override {
    g_pmu = &pmu_;
    if (!pmu_.begin(Wire, pins::ADDR_PMIC, g_sda, g_scl)) {
      Serial.println("power: AXP2101 not found");
      return false;
    }
    Serial.printf("power: AXP2101 chip id 0x%02X\n", pmu_.getChipID());
    // Rails are on by PMIC default. Make the one the factory firmware relies on explicit:
    // ALDO1 3.3 V feeds the analog audio / microphone side.
    pmu_.setALDO1Voltage(3300);
    pmu_.enableALDO1();
    pmu_.enableBattVoltageMeasure();
    pmu_.enableSystemVoltageMeasure();
    pmu_.enableVbusVoltageMeasure();
    ok_ = true;
    return true;
  }
  hal::PowerState read() override {
    hal::PowerState s;
    if (!ok_) return s;
    s.batteryPresent = pmu_.isBatteryConnect();
    s.charging = pmu_.isCharging();
    s.usbPresent = pmu_.isVbusIn();
    s.batteryMillivolts = pmu_.getBattVoltage();
    s.batteryPercent = s.batteryPresent ? pmu_.getBatteryPercent() : -1;
    return s;
  }
  bool buttonPressed() override { return g_expanderOk && g_expander.digitalRead(pins::EXIO_PWR_BUTTON); }
  void shutdown() override {
    if (ok_) pmu_.shutdown();
  }

 private:
  XPowersPMU pmu_;
  bool ok_ = false;
};

Amoled18Display g_display;
Amoled18Touch g_touch;
Amoled18AudioOut g_audioOut;
Amoled18AudioIn g_audioIn;
Amoled18Motion g_motion;
Amoled18Power g_power;

}  // namespace

namespace hal {

bool boardBegin(BoardInfo &info) {
  info.name = "Waveshare ESP32-S3-Touch-AMOLED-1.8";

  pinMode(pins::BOOT_BUTTON, INPUT_PULLUP);

  // 1. I2C.
  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, pins::I2C_HZ);
  g_sda = pins::I2C_SDA;
  g_scl = pins::I2C_SCL;
  scanBus(info);
  info.sda = g_sda;
  info.scl = g_scl;
  if (info.i2cFoundCount == 0) {
    info.revision = "no I2C devices answered";
    return false;
  }

  // 2. Expander and the reset pulse the panel needs.
  g_expanderOk = g_expander.begin(pins::ADDR_EXPANDER, &Wire);
  if (g_expanderOk) {
    pulseResets();
  } else {
    Serial.println("board: TCA9554 expander not found at 0x20; display reset cannot be released");
  }

  // 3. Revision from the touch controller, now that touch has been reset.
  scanBus(info);
  if (i2cPresent(pins::ADDR_TOUCH_CST820)) {
    g_revision = Revision::V2;
    info.revision = "V2 (CO5300 panel, CST820 touch)";
  } else if (i2cPresent(pins::ADDR_TOUCH_FT3168)) {
    g_revision = Revision::V1;
    info.revision = "V1 (SH8601 panel, FT3168 touch)";
  } else {
    g_revision = Revision::Unknown;
    info.revision = "unknown (no touch controller found), assuming V2 panel";
  }
  return true;
}

bool bootButtonPressed() { return digitalRead(pins::BOOT_BUTTON) == LOW; }

bool debugCodecWrite(uint8_t reg, uint8_t value) { return i2cWriteReg(pins::ADDR_CODEC, reg, value); }

void debugRails(Print &out) {
  if (!g_pmu) {
    out.println("pmic: not initialised");
    return;
  }
  out.printf("rails: DC1 %d/%u DC2 %d/%u DC3 %d/%u DC4 %d/%u DC5 %d/%u | ALDO1 %d/%u ALDO2 %d/%u ALDO3 %d/%u ALDO4 %d/%u | BLDO1 %d/%u BLDO2 %d/%u | vbus %u mV sys %u mV\n",
             g_pmu->isEnableDC1(), g_pmu->getDC1Voltage(), g_pmu->isEnableDC2(), g_pmu->getDC2Voltage(),
             g_pmu->isEnableDC3(), g_pmu->getDC3Voltage(), g_pmu->isEnableDC4(), g_pmu->getDC4Voltage(),
             g_pmu->isEnableDC5(), g_pmu->getDC5Voltage(), g_pmu->isEnableALDO1(), g_pmu->getALDO1Voltage(),
             g_pmu->isEnableALDO2(), g_pmu->getALDO2Voltage(), g_pmu->isEnableALDO3(), g_pmu->getALDO3Voltage(),
             g_pmu->isEnableALDO4(), g_pmu->getALDO4Voltage(), g_pmu->isEnableBLDO1(), g_pmu->getBLDO1Voltage(),
             g_pmu->isEnableBLDO2(), g_pmu->getBLDO2Voltage(), g_pmu->getVbusVoltage(), g_pmu->getSystemVoltage());
}

void debugAudio(Print &out) {
  out.printf("mic raw peak: left %d right %d (of 32767)\n", g_micPeakL, g_micPeakR);
  out.printf("amp enable GPIO %d reads %d; i2s %s at %u Hz; codec %s\n", pins::AMP_ENABLE,
             digitalRead(pins::AMP_ENABLE), g_i2sUp ? "up" : "down", (unsigned)g_i2sRate, g_codec ? "created" : "none");
  out.print("ES8311 regs:");
  for (uint8_t r = 0; r <= 0x45; r++) {
    uint8_t v = 0;
    bool ok = i2cReadRegs(pins::ADDR_CODEC, r, &v, 1);
    if (r % 16 == 0) out.printf("\n  %02X:", r);
    if (ok) out.printf(" %02X", v);
    else out.print(" --");
  }
  out.println();
}

Display &display() { return g_display; }
Touch &touch() { return g_touch; }
AudioOut &audioOut() { return g_audioOut; }
AudioIn &audioIn() { return g_audioIn; }
Motion &motion() { return g_motion; }
Power &power() { return g_power; }

}  // namespace hal
