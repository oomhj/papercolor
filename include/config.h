/**
 * PaperColor — Pin Mapping & Configuration
 *
 * Based on official M5Stack PaperColor pin assignment table.
 * Board:  ESP32-S3R8
 * Screen: EL040EF1 (ED2208-DOA) — 4" E6 Full-Color E-Paper
 */

#pragma once

// ============================================================
// System
// ============================================================
#define APP_NAME            "PaperColor"
#define APP_VERSION         "1.0.0"

// ============================================================
// SoC — ESP32-S3R8
// ============================================================
#define SOC_CORE_COUNT      2
#define SOC_CLOCK_HZ        240000000
#define SOC_FLASH_SIZE      (16 * 1024 * 1024)
#define SOC_PSRAM_SIZE      (8 * 1024 * 1024)

// ============================================================
// System I2C Bus (shared) — SCL=G2, SDA=G3
// Devices: RTC (0x32), SHT40 (0x44), M5PM1 (0x6e),
//          ES8311 (0x18), ES7210 (0x40)
// ============================================================
#define PIN_SYS_SCL          2
#define PIN_SYS_SDA          3

// ============================================================
// E-Paper — EL040EF1 (ED2208-DOA) on SPI & GPIO
//   CLK=G15, MOSI=G13, CS=G44, DC=G43, BUSY=G11, RST=G12
//   Power: M5PM1.PYG0 (PY_EPD_EN)
// ============================================================
#define PIN_EPD_SPI_CLK      15
#define PIN_EPD_SPI_MOSI     13
#define PIN_EPD_CS           44
#define PIN_EPD_DC           43
#define PIN_EPD_BUSY         11
#define PIN_EPD_RST          12

#define EPD_WIDTH            400
#define EPD_HEIGHT           600
#define EPD_ROTATION         1
#define EPD_COLOR_DEPTH      8

// ============================================================
// Buttons (active-low, internal pull-up)
//   USER_KEY3 = Button A → G10
//   USER_KEY2 = Button B → G9
//   USER_KEY1 = Button C → G1
//   1× Power button (ON/OFF/RESET/BOOT)
// ============================================================
#define PIN_BTN_A            10
#define PIN_BTN_B            9
#define PIN_BTN_C            1
#define PIN_BTN_PWR          0     // ON/OFF/RESET/BOOT

// ============================================================
// RGB LED — G21 (single data line, likely SK6812/WS2812)
//   Power: M5PM1.LDO3V3_EN_PP (PY_RGB_PWR_EN)
// ============================================================
#define PIN_RGB_DATA         21
#define NUM_RGB_LEDS         2

// ============================================================
// IR Transmitter — G48
// ============================================================
#define PIN_IR_TX            48

// ============================================================
// Audio — ES8311 (codec, 0x18) + ES7210 (mic ADC, 0x40)
//   I2S: MCLK=G42, LRCK=G41, BCLK=G40
//   ES8311 DIN=G39, ES7210 DOUT=G38
//   AUDIO_PWR_EN = G45 (codec + mic power)
//   SPK_EN       = G46 (AW8737A speaker amp enable)
// ============================================================
#define PIN_AUDIO_I2S_MCLK   42
#define PIN_AUDIO_I2S_LRCK   41
#define PIN_AUDIO_I2S_BCLK   40
#define PIN_AUDIO_I2S_DIN    39      // ES8311 data in (codec <- I2S)
#define PIN_AUDIO_I2S_DOUT   38      // ES7210 data out (mic -> I2S)

#define PIN_AUDIO_PWR_EN     45      // ES8311 + ES7210 power
#define PIN_SPK_EN           46      // AW8737A speaker amp enable

#define AUDIO_CODEC_ADDR     0x18    // ES8311
#define AUDIO_ADC_ADDR       0x40    // ES7210

#define AUDIO_SAMPLE_RATE      16000
#define AUDIO_BITS_PER_SAMPLE  16
#define AUDIO_MIC_GAIN         24    // dB
#define AUDIO_SPEAKER_VOLUME   128

// ============================================================
// microSD — SPI
//   CS=G47, CLK=G15, MOSI=G13, MISO=G14
//   Power: M5PM1.PYG3 (PY_SD_PWR_EN)
//   Detect: M5PM1.PYG4 (PY_SD_DET_EN) + PYG1 (CARD_DEC)
// ============================================================
#define PIN_SD_CS            47
#define PIN_SD_CLK           15
#define PIN_SD_MOSI          13
#define PIN_SD_MISO          14

// ============================================================
// RTC — RX8130CE (0x32)
//   IRQ → G7
// ============================================================
#define PIN_RTC_IRQ          7
#define RTC_ADDR             0x32

// ============================================================
// Temperature / Humidity — SHT40 (0x44)
// ============================================================
#define SHT40_ADDR           0x44

// ============================================================
// HY2.0-4P Expansion Port (PORT.A)
//   Black=GND, Red=5V, Yellow=G4, White=G5
//   Power: M5PM1.BOOST5V_EN_PP (PY_GROVE_OUT_EN)
// ============================================================
#define PIN_GROVE_G4         4
#define PIN_GROVE_G5         5

// ============================================================
// PMU — M5PM1 (0x6e)
//   GPIOs: PYG0=EPD_EN, PYG1=CARD_DEC, PYG2=RTC_IRQ,
//          PYG3=SD_PWR_EN, PYG4=SD_DET_EN
//   Rails: DCDC3V3_EN_PP (3V3_L2), LDO3V3_EN_PP (RGB),
//          BOOST5V_EN_PP (Grove)
// ============================================================
#define PMU_ADDR             0x6e

// ============================================================
// Power Characteristics
// ============================================================
#define EPD_VOLTAGE          3.3
#define BATTERY_FULL_V       4.2
#define BATTERY_EMPTY_V      3.0
#define POWER_STANDBY_UA     92.53
#define POWER_ACTIVE_MA      211.97
#define BATTERY_CAPACITY     1250    // mAh
