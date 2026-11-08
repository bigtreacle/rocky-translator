// Hardware layer for the Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1 and V2).
//
// Boot order matters here (DESIGN.md §3): I2C up, expander found, reset lines
// pulsed (or the panel stays black), touch probed to tell V1 from V2, PMIC rails
// confirmed. Only then can the display and the other devices begin().
#include <Adafruit_XCA9554.h>
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <SensorQMI8658.hpp>

#include "../../hal/board.h"
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
    return true;
  }
  hal::TouchPoint read() override {
    hal::TouchPoint p;
    if (!addr_) return p;
    // INT is low while a finger is down. Skip the I2C transaction when it is
    // high and nothing was pressed last time; this is how the datasheet expects
    // the chip to be driven and avoids hammering it between touches.
    if (useInt_ && digitalRead(pins::TP_INT) == HIGH && !lastPressed_) return p;
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
// M0 stub. The ES8311 driver (Espressif's es8311.c as carried by Waveshare's
// Arduino demo) is vendored in M1; until then begin() only drives the amp pin so
// it can be measured, and reports that there is no codec driver yet.

class Amoled18AudioOut : public hal::AudioOut {
 public:
  bool begin(uint32_t sampleRate) override {
    (void)sampleRate;
    pinMode(pins::AMP_ENABLE, OUTPUT);
    setAmpEnabled(true);
    Serial.println("audio out: ES8311 driver not vendored yet (M1); amp enable driven high");
    return false;
  }
  size_t write(const int16_t *, size_t) override { return 0; }
  void setVolume(uint8_t) override {}
  void setAmpEnabled(bool on) override { digitalWrite(pins::AMP_ENABLE, on ? HIGH : LOW); }
};

class Amoled18AudioIn : public hal::AudioIn {
 public:
  bool begin(uint32_t) override { return false; }
  size_t read(int16_t *, size_t) override { return 0; }
  void setGainDb(int) override {}
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

class Amoled18Power : public hal::Power {
 public:
  bool begin() override {
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

Display &display() { return g_display; }
Touch &touch() { return g_touch; }
AudioOut &audioOut() { return g_audioOut; }
AudioIn &audioIn() { return g_audioIn; }
Motion &motion() { return g_motion; }
Power &power() { return g_power; }

}  // namespace hal
