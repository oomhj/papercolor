# Changelog

## [Unreleased]

### v1.0.0 — 2026-07-06

#### Features
- **Daily photo slideshow**: 10 images from Bing, 30min auto-advance
- **Dual mode**: SD card (10-image cache) / no-SD (single image, 30min refresh)
- **Low-power deep sleep**: ESP32 Deep Sleep with RTC timer + button wake
- **Resumable download**: progress persisted to `config.txt` after each image
- **Battery indicator**: 5-segment icon on EPD, real-time I2C reads
- **WiFi provisioning**: AP captive portal + `/sd/wifi.txt` config file
- **Unified config**: `/sd/album/config.txt` (key=value) replaces separate files
- **SPI bus arbitration**: mutex-based for EPD + SD card coexistence
- **EPD fast mode**: `pc_hal_epd_refresh(bool fast)` for quick navigation

#### Bug Fixes
- RTC wake detection: use `esp_sleep_get_wakeup_cause()` not M5PM1 register
- Missing `esp_netif_create_default_wifi_sta()` — WiFi never got IP
- SD card detect: `s_pmu` null pointer + wrong `pinMode` constant
- RX8130 RAM garbage date: `config.txt` as primary source
- Navigation blocked after refresh failure: restore `_total_images` from disk
- Double-free in `load_and_show`: `_decoded_buf` set to null after free
- WiFi disconnect not transitioning to `STA_FAIL` — connection hung 5s

#### Refactoring
- Removed legacy News app
- LED management unified to `wifi_manager` (WiFi state) + `album_app` (app ops)
- `pc_hal_display()` → `pc_hal_epd_refresh(bool fast)`
- WiFi credentials from SD txt → unified `config.txt`
- Deleted hardcoded SSID/PASS from source

#### Documentation
- Full CLAUDE.md with architecture, APIs, pin mapping, sleep flow
- README.md rewritten for open-source
- Hardware docs in `docs/hardware/`
