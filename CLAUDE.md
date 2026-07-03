# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Structure

This repo contains **two coexisting projects** for the M5Stack PaperColor:

- **Root** (`platformio.ini`, `src/`) — PlatformIO / Arduino-framework scaffold
- **`m5_demo/`** (`CMakeLists.txt`, `main/`) — Official M5Stack ESP-IDF v5.5 demo (subtree clone)

Both target the same hardware. The `include/config.h` pin map is the shared truth source.

## Build Commands

### PlatformIO (root)
```
pio run                              # Build
pio run --target upload              # Build + flash
pio run --target upload --upload-port /dev/ttyUSB0
pio device monitor                   # 115200 baud
```

### Official ESP-IDF Demo (m5_demo/)
```
# First time — init submodules + component manager deps
cd m5_demo
git -C components/M5GFX     checkout master  2>/dev/null || true
git -C components/M5Unified checkout master  2>/dev/null || true
idf.py set-target esp32s3
idf.py build                             # Build
idf.py -p /dev/ttyUSB0 flash             # Flash
idf.py -p /dev/ttyUSB0 monitor           # Serial monitor
```

> The demo uses ESP-IDF v5.5.1. Submodules (`components/M5GFX`, `components/M5Unified`) and IDF component manager deps (`esp_tinyusb`, `m5pm1`, `qrcode`, `mdns` per `idf_component.yml`) are required. The `m5_demo/devcontainer.json` already references the same `espressif/idf:release-v5.5` image.

## Hardware Architecture

### SoC: ESP32-S3R8 (240MHz, 16MB Flash + 8MB Octal PSRAM)
PSRAM is critical — the EPD framebuffer (`M5Canvas` at 400×600×8bpp ≈ 240KB) lives there. PSRAM is configured as Octal 40MHz with `malloc()` preferring external RAM below 16KB.

### Key Design Rules

1. **EPD power MUST be enabled before display access** via `pmu.setEPDPower(true)` (M5PM1 PYG0 / PY_EPD_EN).
2. **Audio has two independent power gates**: `PIN_AUDIO_PWR_EN=G45` (ES8311 + ES7210) and `PIN_SPK_EN=G46` (AW8737A speaker amp). Speaker amp defaults OFF.
3. **EPD + microSD share SPI bus** (CLK=G15, MOSI=G13). Never assert both CS simultaneously — EPD CS=G44, SD CS=G47. SD adds MISO=G14.
4. **Single I2C bus** (SCL=G2, SDA=G3) for ALL devices: ES8311(0x18), ES7210(0x40), M5PM1(0x6e), RX8130CE(0x32), SHT40(0x44).
5. **RGB LED is single-wire** (G21, SK6812/WS2812-style). Power switch: M5PM1 LDO3V3_EN_PP. M5Unified's `M5.Led` API drives it.
6. **Buttons are active-low** with internal pull-up.

### Pin Map (from `include/config.h`)

```
EPD:   CLK=G15, MOSI=G13, CS=G44, DC=G43, BUSY=G11, RST=G12
BTN:   A=G10, B=G9, C=G1, PWR=G0
Audio: MCLK=G42, LRCK=G41, BCLK=G40, DIN=G39, DOUT=G38, PWR_EN=G45, SPK_EN=G46
SD:    CS=G47, CLK=G15, MOSI=G13, MISO=G14
I2C:   SCL=G2, SDA=G3
Other: RGB=G21, IR=G48, RTC_IRQ=G7, Grove G4/G5
```

### M5PM1 GPIOs

| PMU Pin | Signal | Controls |
|---------|--------|----------|
| PYG0 | PY_EPD_EN | EPD power rail |
| PYG1 | CARD_DEC | SD card detect |
| PYG2 | RTC_IRQ | RTC wake interrupt |
| PYG3 | PY_SD_PWR_EN | SD module power |
| PYG4 | PY_SD_DET_EN | SD detect pull-up |

Power rails: DCDC3V3_EN_PP (3V3_L2), LDO3V3_EN_PP (RGB), BOOST5V_EN_PP (Grove 5V).

### Power Consumption
- Standby: 92.53 µA • Active: 211.97 mA • Battery: 1250 mAh Li-Po

## ESP-IDF Demo Architecture (m5_demo/main/)

```
main.cpp                 — Entry: hal.init() → detectWakeSource() → app_manager_start() → app_server_init()
├── hal/hal{.cpp,.h}     — Central HAL: init, settings (NVS), SHT40, RX8130CE, power-off, wake source
│   ├── wifi/            — WiFi AP + STA management (ESP-IDF esp_wifi)
│   ├── storage/         — Dual-media storage: SPIFFS (flash) + FAT (SD) with TinyUSB MSC
│   ├── ezdata/          — Ezdata cloud photo fetch API
│   └── utils/
│       ├── audio/       — Tone/melody generation via M5.Speaker
│       ├── image/       — PNG decode + image metadata
│       └── dns_server/  — Captive portal DNS responder
└── apps/
    ├── app_manager/     — State machine: mode switching, button/LED handling, low-power logic
    ├── app_server/      — HTTP config server + captive portal + REST API + index.html
    ├── local_photo_slideshow/ — SD card photo browsing
    └── ezdata_photo_push/    — Cloud photo push display
```

### Application Modes
- **`mode_1` (APP_MODE_LOCAL)**: Browse photos from SD card (or SPI flash fallback). Supports auto-slideshow with configurable interval.
- **`mode_2` (APP_MODE_EZDATA)**: Fetch and display photos from Ezdata cloud service over Wi-Fi.

### Low Power / RTC Wake Cycle
The demo implements a sleep-wake cycle for low-power slideshow:
1. Normal boot → detect wake source via M5PM1 PYG2 (RTC_IRQ)
2. If RTC wake: run one-shot refresh (local or ezdata), schedule next wake via RX8130CE, power off
3. If normal boot: run interactive mode, idle-shutdown after 60s inactivity in low-power mode
4. `scheduleNextWakeMinutes()` writes interval to RX8130CE RAM, then `pm1.shutdown()`

### Storage
M5PM1 PYG3 controls SD power. PYG4 enables SD detect pull-up. PYG1 (CARD_DEC) reads card presence.
The demo uses `hal_storage_switch()` to swap between SPI flash (TinyUSB MSC mode) and SD card.

### Partition Table (m5_demo/partitions.csv)
```
nvs      0x6000
phy      0x1000
factory  0x9F0000  (~10MB application)
storage  0x600000  (6MB FAT — photos)
```

### Key ESP-IDF Config (m5_demo/sdkconfig.defaults)
- PSRAM: Octal 40MHz, CLK=G30, CS=G26, `SPIRAM_USE_MALLOC`
- UART console: TX=G5, RX=G4, secondary USB JTAG
- Wi-Fi: WPA3, SAE, Enterprise all enabled
- LWIP: `max_open_sockets` requires `CONFIG_LWIP_MAX_SOCKETS=16` (app_server needs 13)
- TinyUSB: SUSPEND/RESUME callbacks must be enabled for `hal_storage.cpp`

## E-Paper Display Notes

- `display.setEpdMode(epd_quality)` for best color; `epd_fastest` for QR/boot images.
- After canvas operations: `canvas.pushSprite()` → `display.display()` (or `display.display(dirty_rect)` for partial).
- 8-bit color depth is native for E Ink Spectra 6.

## Warnings

- The PlatformIO env uses board `esp32s3box` as a proxy — PaperColor is not yet an official PlatformIO board target.
- Strapping pins to avoid as general-purpose IO: 0, 1, 2, 3, 8, 9, 18, 43, 46.
- `CORE_DEBUG_LEVEL=5` in PlatformIO is verbose; set to 1 for production.
- Button pins differ between the two projects: PlatformIO scaffold uses config.h defines; ESP-IDF demo uses M5Unified's built-in button detection on the same physical pins.
- The `m5_demo/.gitmodules` points to M5GFX/M5Unified submodules under `components/` — must init before building.
