/**
 * PaperColor — Pin Mapping & Configuration
 *
 * Single source of truth for hardware constants.
 * Board:  M5Stack PaperColor (ESP32-S3R8)
 * Panel:  EL040EF1 (ED2208-DOA) — 4" E6 Full-Color E-Paper
 *
 * I2C Bus (shared, SCL=G2, SDA=G3):
 *   ES8311 (0x18), ES7210 (0x40), M5PM1 (0x6e),
 *   RX8130CE (0x32), SHT40 (0x44)
 *
 * SPI Bus (shared, CLK=G15, MOSI=G13):
 *   EPD CS=G44, SD CS=G47, SD MISO=G14
 */

#pragma once

// ═══════════════════════════════════════════════════════════════
// System
// ═══════════════════════════════════════════════════════════════
#define APP_NAME            "PaperColor"
#define APP_VERSION         "1.0.0"

// ═══════════════════════════════════════════════════════════════
// SoC — ESP32-S3R8
// ═══════════════════════════════════════════════════════════════
#define SOC_CLOCK_HZ        240000000
#define SOC_FLASH_SIZE      (16 * 1024 * 1024)
#define SOC_PSRAM_SIZE      (8 * 1024 * 1024)

// ═══════════════════════════════════════════════════════════════
// I2C Bus — SCL=G2, SDA=G3
// ═══════════════════════════════════════════════════════════════
#define PIN_SYS_SCL          2
#define PIN_SYS_SDA          3

// ═══════════════════════════════════════════════════════════════
// E-Paper — EL040EF1 on SPI (CLK=G15, MOSI=G13)
// ═══════════════════════════════════════════════════════════════
#define PIN_EPD_SPI_CLK      15
#define PIN_EPD_SPI_MOSI     13
#define PIN_EPD_CS           44
#define PIN_EPD_DC           43
#define PIN_EPD_BUSY         11
#define PIN_EPD_RST          12

#define EPD_WIDTH            400
#define EPD_HEIGHT           600
#define EPD_ROTATION         3
#define EPD_COLOR_DEPTH      8

// ═══════════════════════════════════════════════════════════════
// Buttons (active-low, internal pull-up)
// ═══════════════════════════════════════════════════════════════
#define PIN_BTN_TOP          1     // BTN-TOP (physical top button)
#define PIN_BTN_UP           9     // BTN-UP   (up / previous)
#define PIN_BTN_DOWN         10    // BTN-DOWN (down / next)
#define PIN_BTN_PWR          0     // ON/OFF/RESET/BOOT

// ═══════════════════════════════════════════════════════════════
// RGB LED — G21 (single wire, SK6812/WS2812)
// ═══════════════════════════════════════════════════════════════
#define PIN_RGB_DATA         21
#define NUM_RGB_LEDS         2

// ═══════════════════════════════════════════════════════════════
// IR Transmitter — G48
// ═══════════════════════════════════════════════════════════════
#define PIN_IR_TX            48

// ═══════════════════════════════════════════════════════════════
// Audio — ES8311 (0x18) + ES7210 (0x40) + AW8737A
// ═══════════════════════════════════════════════════════════════
#define PIN_AUDIO_I2S_MCLK   42
#define PIN_AUDIO_I2S_LRCK   41
#define PIN_AUDIO_I2S_BCLK   40
#define PIN_AUDIO_I2S_DIN    39      // ES8311 DIN
#define PIN_AUDIO_I2S_DOUT   38      // ES7210 DOUT
#define PIN_AUDIO_PWR_EN     45      // ES8311 + ES7210 power gate
#define PIN_SPK_EN           46      // AW8737A speaker amp enable

#define AUDIO_CODEC_ADDR     0x18
#define AUDIO_ADC_ADDR       0x40

#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_BITS_PER_SAMPLE  16
#define AUDIO_MIC_GAIN         24
#define AUDIO_SPEAKER_VOLUME   128

// ═══════════════════════════════════════════════════════════════
// microSD — SPI (shared bus with EPD)
// ═══════════════════════════════════════════════════════════════
#define PIN_SD_CS            47
#define PIN_SD_CLK           15
#define PIN_SD_MOSI          13
#define PIN_SD_MISO          14

// ═══════════════════════════════════════════════════════════════
// RTC — RX8130CE (0x32)
// ═══════════════════════════════════════════════════════════════
#define PIN_RTC_IRQ          7
#define RTC_ADDR             0x32

// ═══════════════════════════════════════════════════════════════
// SHT40 Temperature / Humidity (0x44)
// ═══════════════════════════════════════════════════════════════
#define SHT40_ADDR           0x44

// ═══════════════════════════════════════════════════════════════
// HY2.0-4P Expansion Port (PORT.A)
// ═══════════════════════════════════════════════════════════════
#define PIN_GROVE_G4         4
#define PIN_GROVE_G5         5

// ═══════════════════════════════════════════════════════════════
// PMU — M5PM1 (0x6e)
// ═══════════════════════════════════════════════════════════════
#define PMU_ADDR             0x6e

// ═══════════════════════════════════════════════════════════════
// Power
// ═══════════════════════════════════════════════════════════════
#define EPD_VOLTAGE          3.3
#define BATTERY_FULL_V       4.2
#define BATTERY_EMPTY_V      3.0
#define POWER_STANDBY_UA     92.53
#define POWER_ACTIVE_MA      211.97
#define BATTERY_CAPACITY     1250    // mAh
