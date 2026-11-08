// Eridian Translator: M0 bring-up firmware.
//
// Proves the board: PSRAM and flash sizes, I2C devices, board revision, display
// test pattern, touch, IMU, PMIC, amp-enable pin. Serial commands on the USB CDC
// port:  b <0-100>  brightness   p 0|1  panel power   a 0|1  amp enable   s  rescan I2C
#include <Arduino.h>
#include <Wire.h>

#include "hal/board.h"
#include "ui/bringup_screen.h"
#include "ui/lvgl_port.h"
#include "version.h"

static hal::BoardInfo g_info;
static bool g_touchOk = false, g_motionOk = false, g_powerOk = false, g_displayOk = false;
static char g_subtitle[160];

static void printScan(const hal::BoardInfo &info) {
  Serial.printf("I2C on SDA %d / SCL %d, %d device(s):", info.sda, info.scl, info.i2cFoundCount);
  for (int i = 0; i < info.i2cFoundCount; i++) {
    uint8_t a = info.i2cFound[i];
    const char *who = a == 0x20 ? "TCA9554" : a == 0x15 ? "CST820" : a == 0x38 ? "FT3168" : a == 0x34 ? "AXP2101"
                    : a == 0x18 ? "ES8311" : a == 0x6B ? "QMI8658" : a == 0x51 ? "PCF85063" : "?";
    Serial.printf(" 0x%02X(%s)", a, who);
  }
  Serial.println();
}

static void handleSerial() {
  static char line[32];
  static size_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line[n] = 0;
      n = 0;
      int v = atoi(line + 1);
      switch (line[0]) {
        case 'b': hal::display().setBrightness((uint8_t)v); Serial.printf("brightness %d%%\n", v); break;
        case 'p': hal::display().setPower(v != 0); Serial.printf("panel %s\n", v ? "on" : "off"); break;
        case 'a': hal::audioOut().setAmpEnabled(v != 0); Serial.printf("amp enable %d\n", v); break;
        case 's': { hal::BoardInfo i = g_info; /* rescan on the live bus */
                    i.i2cFoundCount = 0;
                    for (uint8_t a = 0x08; a < 0x78; a++) {
                      Wire.beginTransmission(a);
                      if (Wire.endTransmission() == 0 && i.i2cFoundCount < 16) i.i2cFound[i.i2cFoundCount++] = a;
                    }
                    printScan(i); break; }
        case 0: break;
        default: Serial.println("commands: b <0-100>, p 0|1, a 0|1, s");
      }
    } else if (n < sizeof line - 1) {
      line[n++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  // USB CDC: when no host is reading, a write would otherwise block for 100 ms
  // and starve LVGL and touch. Drop output instead.
  Serial.setTxTimeoutMs(0);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);  // USB CDC needs a moment
  delay(200);

  Serial.println();
  Serial.printf("Eridian Translator %s (%s) M0 bring-up\n", FIRMWARE_VERSION, BOARD_VARIANT);
  Serial.printf("chip %s rev %d, %d cores at %u MHz\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned)ESP.getCpuFreqMHz());
  Serial.printf("flash %u MB, PSRAM %u KB (expect 16 MB and ~8192 KB)\n",
                (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)), (unsigned)(ESP.getPsramSize() / 1024));
  Serial.printf("heap %u KB free, largest internal block %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024),
                (unsigned)(ESP.getMaxAllocHeap() / 1024));

  bool boardOk = hal::boardBegin(g_info);
  printScan(g_info);
  Serial.printf("board: %s, revision %s\n", g_info.name, g_info.revision);
  if (!boardOk) Serial.println("board: bring-up stopped, check the I2C pins and the expander");

  g_displayOk = hal::display().begin();
  Serial.printf("display: %s\n", g_displayOk ? "ok" : "FAILED");
  g_touchOk = hal::touch().begin();
  Serial.printf("touch: %s %s\n", g_touchOk ? "ok" : "FAILED", hal::touch().chipName());
  g_motionOk = hal::motion().begin();
  Serial.printf("motion: %s\n", g_motionOk ? "ok" : "FAILED");
  g_powerOk = hal::power().begin();
  Serial.printf("power: %s\n", g_powerOk ? "ok" : "FAILED");
  hal::audioOut().begin(22050);  // M0: only drives GPIO 46 high so it can be measured
  Serial.println("amp enable GPIO 46 driven high: measure it (expect 3.3 V)");

  if (g_displayOk && ui::lvglBegin(hal::display(), hal::touch())) {
    snprintf(g_subtitle, sizeof g_subtitle, "%s\n%s\nI2C SDA %d SCL %d, %d devices\ntouch %s", FIRMWARE_VERSION,
             g_info.revision, g_info.sda, g_info.scl, g_info.i2cFoundCount, hal::touch().chipName());
    ui::bringupCreate("ERIDIAN M0", g_subtitle);
    Serial.println("lvgl: test screen up");
  }
  Serial.println("ready");
}

void loop() {
  ui::lvglLoop();
  handleSerial();

  static uint32_t lastStatus = 0;
  static hal::TouchPoint lastTouch;
  static hal::Accel accel;
  static char status[200];

  hal::Accel a;
  if (g_motionOk && hal::motion().read(a)) accel = a;

  hal::TouchPoint tp = g_touchOk ? hal::touch().read() : hal::TouchPoint{};
  if (tp.pressed != lastTouch.pressed || (tp.pressed && (abs(tp.x - lastTouch.x) > 2 || abs(tp.y - lastTouch.y) > 2))) {
    ui::bringupSetTouch(tp.pressed, tp.x, tp.y);
    if (tp.pressed) Serial.printf("touch %d,%d\n", tp.x, tp.y);
    lastTouch = tp;
  }

  if (millis() - lastStatus >= 1000) {
    lastStatus = millis();
    hal::PowerState ps = g_powerOk ? hal::power().read() : hal::PowerState{};
    bool btn = g_powerOk && hal::power().buttonPressed();
    snprintf(status, sizeof status,
             "accel  x %+.2f  y %+.2f  z %+.2f g\n"
             "battery %s %u mV %d%%  %s%s\n"
             "power button %s\n"
             "up %lu s   heap %u KB",
             accel.x, accel.y, accel.z, ps.batteryPresent ? "present" : "absent", ps.batteryMillivolts,
             ps.batteryPercent, ps.charging ? "charging" : "not charging", ps.usbPresent ? ", USB in" : "",
             btn ? "HELD" : "released", (unsigned long)(millis() / 1000), (unsigned)(ESP.getFreeHeap() / 1024));
    ui::bringupSetStatus(status);
    Serial.printf("imu %+.2f %+.2f %+.2f | bat %u mV %d%% %s%s | btn %d | heap %u KB\n", accel.x, accel.y, accel.z,
                  ps.batteryMillivolts, ps.batteryPercent, ps.charging ? "chg" : "dis", ps.usbPresent ? " usb" : "",
                  btn, (unsigned)(ESP.getFreeHeap() / 1024));
  }
  delay(5);
}
