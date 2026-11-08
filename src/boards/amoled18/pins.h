// Waveshare ESP32-S3-Touch-AMOLED-1.8: every pin number and I2C address for this
// board. Nothing outside src/boards/amoled18/ may contain a pin number.
//
// Sources: Waveshare's examples/arduino/libraries/Mylibrary/pin_config.h, the
// ESP-IDF BSP waveshare/esp32_s3_touch_amoled_1_8 2.0.3, and the schematic.
// Collected 2026-11-07; see DESIGN.md Appendix A. Items marked CHECK are the
// day-one hardware questions.
#pragma once
#include <stdint.h>

namespace pins {

// I2C. Confirmed on the real board 2026-11-08: SDA 15 / SCL 14 (Waveshare's
// pin_config.h). The Arduino core's variant file for this board has them the
// other way round, so never use its SDA/SCL constants.
constexpr int I2C_SDA = 15;
constexpr int I2C_SCL = 14;
constexpr uint32_t I2C_HZ = 400000;

constexpr uint8_t ADDR_EXPANDER = 0x20;  // TCA9554
constexpr uint8_t ADDR_TOUCH_CST820 = 0x15;  // V2
constexpr uint8_t ADDR_TOUCH_FT3168 = 0x38;  // V1
constexpr uint8_t ADDR_PMIC = 0x34;  // AXP2101
constexpr uint8_t ADDR_CODEC = 0x18;  // ES8311
constexpr uint8_t ADDR_IMU = 0x6B;  // QMI8658
constexpr uint8_t ADDR_RTC = 0x51;  // PCF85063

// TCA9554 expander pins.
constexpr uint8_t EXIO_LCD_RESET = 0;
constexpr uint8_t EXIO_LCD_POWER = 1;  // DSI_PWR_EN
constexpr uint8_t EXIO_TP_RESET = 2;
constexpr uint8_t EXIO_RTC_INT = 3;
constexpr uint8_t EXIO_PWR_BUTTON = 4;  // SYS_OUT, high while the power button is held (wiki)
constexpr uint8_t EXIO_PMIC_IRQ = 5;    // AXP2101 IRQ, active low
constexpr uint8_t EXIO_IMU_INT1 = 6;
constexpr uint8_t EXIO_SD_CS = 7;       // keep high; SD is unused

// AMOLED over QSPI (SPI2). No reset GPIO: reset is EXIO_LCD_RESET.
constexpr int LCD_CS = 12;
constexpr int LCD_SCLK = 11;
constexpr int LCD_D0 = 4;
constexpr int LCD_D1 = 5;
constexpr int LCD_D2 = 6;
constexpr int LCD_D3 = 7;
constexpr int LCD_TE = 13;  // wired, unused by every demo
constexpr int LCD_WIDTH = 368;
constexpr int LCD_HEIGHT = 448;
constexpr int32_t LCD_QSPI_HZ = 40000000;
constexpr uint8_t LCD_COL_OFFSET_V2 = 16;  // CO5300 panel on V2 boards

// Touch.
constexpr int TP_INT = 21;  // active low; the only interrupt on a real GPIO
// CHECK on a V2 board: Waveshare's BSP applies a 16 px x gap to the CST820 as
// well as to the panel. If touch lands 16 px right of the finger, set this to -16.
constexpr int16_t TOUCH_X_OFFSET_V2 = 0;

// I2S to the ES8311 codec.
constexpr int I2S_MCLK = 16;
constexpr int I2S_BCLK = 9;
constexpr int I2S_WS = 45;
constexpr int I2S_DOUT = 8;  // ESP -> codec DAC (DSDIN)
constexpr int I2S_DIN = 10;  // codec ADC -> ESP (ASDOUT)

// NS4150B speaker amplifier enable, active high. GPIO 46 is a strapping pin:
// configure it only after boot. CHECK with a meter that it reaches 3.3 V
// (Waveshare issue #13 reports one V2 board where it would not).
constexpr int AMP_ENABLE = 46;

constexpr int BOOT_BUTTON = 0;

}  // namespace pins
