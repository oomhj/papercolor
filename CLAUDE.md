# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Development Principles

- **先沟通，后实现** — 除非用户明确指令，不要直接写代码。先确认需求、理解设计，达成一致后再动手。
- **设计方案先行** — 复杂功能先输出文档（需求分析、方案对比、架构设计），等用户确认后再编码。

## Project Overview

M5Stack PaperColor firmware built with ESP-IDF v5.5. Two coexisting projects in one repo:

- **Root** (`CMakeLists.txt`, `main/`) — Active development project with custom apps (album, news)
- **`m5_demo/`** — Official M5Stack demo (reference only, subtree clone)

The `main.cpp` entry point is **application-specific** — currently runs Album. Switch apps by editing `main/main.cpp`.

## Build & Flash

```bash
# Build (in Docker container)
docker exec papercolor-build bash -c \
  ". /opt/esp/idf/export.sh > /dev/null 2>&1 && idf.py build"

# Flash (host esptool, port may change: usbmodem1101 / usbmodem2101)
esptool.py --chip esp32s3 -p /dev/tty.usbmodem1101 -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/papercolor_template.bin

# Serial monitor
idf.py -p /dev/tty.usbmodem1101 monitor
```

Build container:
```bash
docker run -d --rm --name papercolor-build \
  -v .:/workspaces/PaperColor -w /workspaces/PaperColor \
  espressif/idf:release-v5.5 sleep infinity
```

Full build/flash guide: `docs/build-guide.md`

## Application Architecture

```
main/
├── main.cpp              — Entry point, runs AlbumApp
├── hal/
│   ├── hal.h/cpp         — pc_hal_* API: init, EPD, battery, SHT40, deep sleep
│   ├── spi_bus.h/cpp     — SPI2_HOST arbiter (mutex: EPD vs SD card)
│   ├── sd_card.h/cpp     — SD card mount/unmount/lock/unlock
│   ├── led_driver.h/cpp  — LED control wrapper
│   └── button.h          — BTN_UP/DOWN/TOP aliases
├── apps/
│   ├── album/            — Daily photo slideshow (current main)
│   │   ├── album_app.h   — Lifecycle + slideshow state
│   │   └── album_app.cpp — SD cache, WiFi update, JPEG decode, EPD display
│   ├── news/             — RSS news reader
│   │   ├── news_app.{h,cpp}    — Lifecycle + main logic
│   │   ├── news_fetcher.{h,cpp} — HTTP download wrapper
│   │   └── news_parser.{h,cpp}  — RSS XML tag parser
│   └── template/         — App lifecycle template (reference)
└── wifi/
    ├── wifi_manager.h/cpp         — STA+AP management, NVS config, retry with backoff
    └── wifi_provisioning.h/cpp    — Captive portal: DNS hijack + HTTP config page
```

### App Lifecycle
```cpp
class MyApp {
    bool init();        // allocate resources
    void deinit();      // free resources
    void start();       // start running
    void stop();        // stop
    void update();      // called periodically from main loop
    void refresh();     // manual trigger
};
```

### HAL API (`pc_hal_*`)
```c
void pc_hal_init(void);           // I2C → M5.begin() → Canvas → PMU → power rails → spi_bus_init()
void pc_hal_update(void);         // M5.update()
M5Canvas* g_canvas;               // off-screen canvas in PSRAM
void pc_hal_display(void);        // pushSprite(0,0) — no SPI lock, no display()
void pc_hal_epd_refresh(void);    // pushSprite + display() with SPI claim/release
uint16_t pc_hal_read_battery_mv(void);
float pc_hal_battery_pct(void);
bool pc_hal_is_charging(void);
void pc_hal_set_epd_power(bool on);
void pc_hal_deep_sleep(void);
bool pc_hal_read_sht40(float* temp_c, float* humidity);
void pc_hal_draw_splash(void);
void pc_hal_show_power_info(void);
```

### SPI Bus Arbiter (`spi_bus_*`)
```c
void spi_bus_init(void);                        // create mutex (called in pc_hal_init)
bool spi_bus_claim(spi_owner_t owner, uint32_t timeout_ms);  // OWNER_EPD / OWNER_SD
void spi_bus_release(void);
```

### SD Card (`sd_card_*`)
```c
bool sd_card_mount(void);        // power on + mount FatFS at /sd
void sd_card_unmount(void);      // sync + power off
bool sd_card_detect(void);       // check physical presence
bool sd_card_mounted(void);      // check mount state
bool sd_card_lock(uint32_t timeout_ms);   // claim SPI for SD operations
void sd_card_unlock(void);       // release SPI after SD
```

## Hardware Rules

### Init Sequence (strict order)
1. I2C bus recovery (9 clock pulses if not cold boot)
2. `M5.begin()` — I2C, buttons, display driver
3. Create `M5Canvas` in PSRAM (8-bit, 400×600)
4. `M5PM1.begin()` — power management
5. EPD power: PYG0 HIGH
6. Audio power: G45 HIGH, G46 LOW (amp default OFF)
7. Battery check: shutdown if < 3.1V

### Key Constraints
- **EPD must be powered** before any display operation (PYG0)
- **EPD + SD share SPI2_HOST** (CLK=G15, MOSI=G13, MISO=G14) — use `spi_bus_claim/release` for coordination
- **SD card power** controlled by M5PM1 PYG3 (PY_SD_PWR_EN), set HIGH in `pc_hal_init()`
- **Single I2C bus** for ALL devices: ES8311(0x18), ES7210(0x40), M5PM1(0x6e), RX8130CE(0x32), SHT40(0x44)
- **RGB LED** single-wire G21, SK6812/WS2812 via `M5.Led`
- **Buttons** active-low, internal pull-up: A=G10, B=G9, C=G1, PWR=G0
- **Strapping pins** avoid as GPIO: 0, 1, 2, 3, 8, 9, 18, 43, 46

### EPD Display
- Native color depth: 8-bit (E Ink Spectra 6)
- Refresh modes: `epd_quality` (best color), `epd_fastest` (fast, low quality)
- Render flow: draw to `g_canvas` → `g_canvas->pushSprite(0,0)` → `M5.Display.display()`

## WiFi Manager

`wifi_manager` provides full STA + AP management with NVS-backed config (up to 3 saved networks).

- Auto-retry with backoff (3 attempts, breathing LED)
- Runtime disconnect detection → reconnect loop
- AP provisioning mode with captive portal (DNS hijack + HTTP server)
- LED breathing: slow blue (connecting), green 3s (success), fast blue (provisioning)
- Button: BTN-B (G9) long press 3s → provisioning; BTN-C (G1) long press 5s → sleep

Note: Album app uses `wifi_manager` (NVS-saved networks + hardcoded fallback). News app still uses independent hardcoded WiFi.

## Current Apps

### Album (Daily Photo Slideshow)
- `main/main.cpp` runs this app
- **Dual mode**: with SD card (10-image slideshow) / without SD (single image)
- **SD mode**: stores `/sd/album/1..10.jpg` + `index.txt` (date of last update)
  - Shows cached image immediately on boot, then checks for daily update
  - Daily update: download 1 new image → show → queue rest 9 in background
  - TOP long press: re-download all 10 (same incremental pattern)
  - Auto-advance every 30min, UP/DOWN for manual navigation
  - UP+DOWN held together → WiFi provisioning captive portal
- **No-SD mode**: fetch 1 image via HTTP, display, auto-refresh every 30min
- WiFi: uses `wifi_manager` (NVS-backed saved networks) with hardcoded fallback
- Fetch: `https://bing.img.run/rand_1366x768.php` → 302 redirect → JPEG
- Rendering: `esp_new_jpeg` decode → Floyd-Steinberg dither → EPD

### News (RSS Reader)
- Uses 少数派(sspai) RSS feed: `https://sspai.com/feed`
- `news_fetcher` → `news_parser` (tag-based RSS 2.0 parser) → render
- Display: title (Font6) + source + description, no image support in P0

## Development Tips

- Serial port may change on reconnect (`/dev/tty.usbmodem1101` → `2101`); check with `ls /dev/tty.*`
- Docker container holds serial port; run `docker rm -f papercolor-build` before flashing
- After flash, container needs restart for next build
- Enable verbose logging: `CORE_DEBUG_LEVEL=5` in `sdkconfig.defaults`
- `esp_http_client_get_header()` gets **request** headers, not response headers; use event handler for response
- `esp_http_client_config_t.host` is the TCP connection target (can be IP), NOT the Host header — the URL's domain handles HTTP Host + TLS SNI
- Gzip decompression: ESP-IDF v5.5 esp_http_client has no built-in gzip; use miniz `tinfl_decompress_mem_to_mem()` from `esp_rom/include/miniz.h`
