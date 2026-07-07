/**
 * PaperColor — Hardware Abstraction Layer Implementation
 *
 * Init sequence (MUST follow this order):
 *   1. I2C bus recovery (if not cold boot)
 *   2. M5.begin() — initializes I2C, buttons, basic GPIO
 *   3. Create off-screen canvas (PSRAM-backed)
 *   4. M5PM1.begin() — power management
 *   5. Enable power rails: EPD, Audio as needed
 *   6. Sensors (SHT40, RTC) are on I2C, auto-detected
 */

#include "hal.h"
#include "config.h"
#include "led_driver.h"
#include <cstdio>
#include <ctime>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <M5PM1.h>
M5PM1* s_pmu = NULL;

static const char* TAG = "HAL";

M5Canvas* g_canvas = nullptr;

// ── I2C Bus Recovery ─────────────────────────────────────────
static void i2c_bus_recovery()
{
    ESP_LOGW(TAG, "I2C bus recovery");

    gpio_config_t cfg = {};
    cfg.pin_bit_mask  = (1ULL << PIN_SYS_SCL) | (1ULL << PIN_SYS_SDA);
    cfg.mode          = GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pull_up_en    = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&cfg);

    gpio_set_level((gpio_num_t)PIN_SYS_SDA, 1);
    gpio_set_level((gpio_num_t)PIN_SYS_SCL, 1);
    esp_rom_delay_us(10);

    for (int i = 0; i < 9; i++) {
        gpio_set_level((gpio_num_t)PIN_SYS_SCL, 0);
        esp_rom_delay_us(5);
        gpio_set_level((gpio_num_t)PIN_SYS_SCL, 1);
        esp_rom_delay_us(5);
        if (gpio_get_level((gpio_num_t)PIN_SYS_SDA) == 1) break;
    }

    gpio_set_level((gpio_num_t)PIN_SYS_SDA, 0);
    esp_rom_delay_us(5);
    gpio_set_level((gpio_num_t)PIN_SYS_SCL, 1);
    esp_rom_delay_us(5);
    gpio_set_level((gpio_num_t)PIN_SYS_SDA, 1);
    esp_rom_delay_us(5);

    gpio_reset_pin((gpio_num_t)PIN_SYS_SCL);
    gpio_reset_pin((gpio_num_t)PIN_SYS_SDA);
}

// ── Init ─────────────────────────────────────────────────────

void pc_hal_init(void)
{
    // I2C recovery on non-poweron resets
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason != ESP_RST_POWERON) {
        i2c_bus_recovery();
    }

    // M5 Unified init (I2C, buttons, display driver)
    auto cfg          = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);

    // EPD quality mode
    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.setRotation(3);

    // Off-screen canvas in PSRAM
    g_canvas = new M5Canvas(&M5.Display);
    g_canvas->createSprite(M5.Display.width(), M5.Display.height());

    ESP_LOGI(TAG, "Display: %d x %d (%d-bit)",
             M5.Display.width(), M5.Display.height(),
             M5.Display.getColorDepth());

    // ── M5PM1 Power Management ──
    s_pmu = new M5PM1();
    s_pmu->begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    s_pmu->setI2cConfig(0);

    // EPD power rail on (PYG0 / PY_EPD_EN)
    s_pmu->pinMode(M5PM1_GPIO_NUM_0, OUTPUT);
    s_pmu->digitalWrite(M5PM1_GPIO_NUM_0, HIGH);

    // Audio codec power on (G45) — use ESP-IDF GPIO
    gpio_set_direction((gpio_num_t)PIN_AUDIO_PWR_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_AUDIO_PWR_EN, 1);

    // Speaker amp off by default (G46)
    gpio_set_direction((gpio_num_t)PIN_SPK_EN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_SPK_EN, 0);

    // Enable charging + boost
    s_pmu->setChargeEnable(true);
    s_pmu->setBoostEnable(true);

    // SD card: power on, card detect circuit (like the demo)
    s_pmu->pinMode(M5PM1_GPIO_NUM_3, OUTPUT);  // PY_SD_PWR_EN
    s_pmu->digitalWrite(M5PM1_GPIO_NUM_3, HIGH);
    s_pmu->pinMode(M5PM1_GPIO_NUM_4, OUTPUT);  // SD_DET_EN
    s_pmu->digitalWrite(M5PM1_GPIO_NUM_4, HIGH);
    s_pmu->pinMode(M5PM1_GPIO_NUM_1, INPUT_PULLUP);  // CARD_DEC

    // Check battery — shutdown if critically low (< 3.1V)
    uint16_t battery_mv = 0;
    if (s_pmu->readVbat(&battery_mv) == M5PM1_OK && battery_mv < 3100) {
        ESP_LOGE(TAG, "Battery critically low (%dmV), shutting down", battery_mv);
        s_pmu->shutdown();
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    // Init SPI bus arbiter (mutex only — M5GFX owns physical bus)
    spi_bus_init();

    // ── RTC wake pin (M5PM1 GPIO2 connected to RX8130 IRQ) ──
    s_pmu->gpioSetFunc(M5PM1_GPIO_NUM_2, M5PM1_GPIO_FUNC_WAKE);
    s_pmu->gpioSetPull(M5PM1_GPIO_NUM_2, M5PM1_GPIO_PULL_UP);
    s_pmu->gpioSetWakeEdge(M5PM1_GPIO_NUM_2, M5PM1_GPIO_WAKE_FALLING);
    s_pmu->gpioSetWakeEnable(M5PM1_GPIO_NUM_2, true);

    ESP_LOGI(TAG, "HAL init complete — Battery: %dmV", battery_mv);
}

void pc_hal_update(void)
{
    M5.update();
}

// ── Display ──────────────────────────────────────────────────

void pc_hal_display(void)
{
    if (g_canvas) {
        g_canvas->pushSprite(0, 0);
    }
}

void pc_hal_epd_refresh(bool fast)
{
    if (!g_canvas) return;
    if (!spi_bus_claim(SPI_OWNER_EPD, UINT32_MAX)) {
        ESP_LOGE(TAG, "EPD claim failed — display skipped");
        return;
    }
    M5.Display.setEpdMode(fast ? epd_mode_t::epd_fastest : epd_mode_t::epd_quality);
    g_canvas->pushSprite(0, 0);
    M5.Display.display();
    spi_bus_release();
}

// ── Power ────────────────────────────────────────────────────

uint16_t pc_hal_read_battery_mv(void)
{
    M5PM1 pmu;
    uint16_t mv = 0;
    pmu.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    if (pmu.readVbat(&mv) == M5PM1_OK) {
        return mv;
    }
    return 0;
}

float pc_hal_battery_pct(void)
{
    uint16_t mv = pc_hal_read_battery_mv();
    if (mv == 0) return 0.0f;
    if (mv >= (uint16_t)(BATTERY_FULL_V * 1000)) return 100.0f;
    if (mv <= (uint16_t)(BATTERY_EMPTY_V * 1000)) return 0.0f;
    return (float)(mv - (uint16_t)(BATTERY_EMPTY_V * 1000)) * 100.0f /
           (float)((BATTERY_FULL_V - BATTERY_EMPTY_V) * 1000);
}

bool pc_hal_is_charging(void)
{
    M5PM1 pmu;
    pmu.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    m5pm1_pwr_src_t src = M5PM1_PWR_SRC_UNKNOWN;
    if (pmu.getPowerSource(&src) == M5PM1_OK) {
        return (src == M5PM1_PWR_SRC_5VIN || src == M5PM1_PWR_SRC_5VINOUT);
    }
    return false;
}

void pc_hal_set_epd_power(bool on)
{
    M5PM1 pmu;
    pmu.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    pmu.pinMode(M5PM1_GPIO_NUM_0, OUTPUT);
    pmu.digitalWrite(M5PM1_GPIO_NUM_0, on ? HIGH : LOW);
}

void pc_hal_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep");
    led_before_sleep();  // SK6812 holds last color — turn off before CPU stops
    M5.Display.sleep();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Clear all stale wakeup sources, then set GPIO wakeup
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 1) | (1ULL << 9) | (1ULL << 10),
                                  ESP_EXT1_WAKEUP_ANY_LOW);

    esp_deep_sleep_start();
}

// ── Low-Power Sleep (RTC wake) ────────────────────────────────

bool pc_hal_is_rtc_wake(void)
{
    // ESP32 deep sleep: check if wake was from internal RTC timer
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
}

bool pc_hal_schedule_wake(uint32_t minutes)
{
    m5::rtc_date_t d;
    m5::rtc_time_t t;
    if (!M5.Rtc.getDateTime(&d, &t)) {
        ESP_LOGE(TAG, "RTC not available");
        return false;
    }

    struct tm tm = {};
    tm.tm_year  = d.year + 100;
    tm.tm_mon   = d.month - 1;
    tm.tm_mday  = d.date;
    tm.tm_hour  = t.hours;
    tm.tm_min   = t.minutes + (int)minutes;
    tm.tm_sec   = t.seconds;
    if (mktime(&tm) < 0) { ESP_LOGE(TAG, "mktime failed"); return false; }

    int ret = M5.Rtc.setAlarmIRQ(&tm);
    ESP_LOGI(TAG, "Wake scheduled in %u min (%04d-%02d-%02d %02d:%02d:%02d)",
             minutes, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return ret > 0;
}

void pc_hal_power_off_scheduled(uint32_t minutes)
{
    M5.Display.sleep();
    vTaskDelay(pdMS_TO_TICKS(100));

    if (minutes > 0) {
        pc_hal_schedule_wake(minutes);
    }

    // Clear PMU wake flags + RTC IRQ
    uint8_t src = 0;
    s_pmu->getWakeSource(&src, M5PM1_CLEAN_ALL);
    M5.Rtc.clearIRQ();

    ESP_LOGI(TAG, "Powering off (next wake in %u min)", minutes);

    // M5PM1 sys command: shutdown (disconnects all power rails)
    s_pmu->sysCmd(M5PM1_SYS_CMD_OFF);

    // Fallback — should be unreachable
    while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

bool pc_hal_rtc_ram_write(uint8_t index, uint8_t value)
{
    if (index > 3) return false;
    if (!M5.In_I2C.start(0x32, false, 400000)) return false;
    uint8_t buf[] = {(uint8_t)(0x20 + index), value};
    M5.In_I2C.write(buf, 2);
    M5.In_I2C.stop();
    return true;
}

bool pc_hal_rtc_ram_read(uint8_t index, uint8_t* value)
{
    if (index > 3 || !value) return false;
    uint8_t reg = 0x20 + index;
    if (!M5.In_I2C.start(0x32, false, 400000)) return false;
    M5.In_I2C.write(&reg, 1);
    M5.In_I2C.stop();
    if (!M5.In_I2C.start(0x32, true, 400000)) return false;
    M5.In_I2C.read(value, 1);
    M5.In_I2C.stop();
    return true;
}

// ── SHT40 Sensor ─────────────────────────────────────────────

bool pc_hal_read_sht40(float* temp_c, float* humidity)
{
    uint8_t cmd = 0xFD;  // high-precision measurement
    uint8_t buf[6];

    if (!M5.In_I2C.start(SHT40_ADDR, false, 400000)) return false;
    M5.In_I2C.write(&cmd, 1);
    M5.In_I2C.stop();

    vTaskDelay(pdMS_TO_TICKS(10));

    if (!M5.In_I2C.start(SHT40_ADDR, true, 400000)) return false;
    M5.In_I2C.read(buf, 6);
    M5.In_I2C.stop();

    uint16_t raw_t = (buf[0] << 8) | buf[1];
    uint16_t raw_h = (buf[3] << 8) | buf[4];

    *temp_c   = -45.0f + 175.0f * raw_t / 65535.0f;
    *humidity = -6.0f  + 125.0f * raw_h / 65535.0f;
    if (*humidity > 100.0f) *humidity = 100.0f;
    if (*humidity < 0.0f)   *humidity = 0.0f;

    return true;
}

// ── Color constants (M5GFX TFT_* equivalents for header) ─────
#ifndef TFT_DARKBLUE
#define TFT_DARKBLUE   0x0010
#endif
#ifndef TFT_DARKGREEN
#define TFT_DARKGREEN  0x0400
#endif
#ifndef TFT_DARKGRAY
#define TFT_DARKGRAY   0x528A
#endif

// ── Demo Screens ─────────────────────────────────────────────

void pc_hal_draw_splash(void)
{
    const int w = g_canvas->width();
    const int h = g_canvas->height();

    g_canvas->fillScreen(TFT_WHITE);

    // Header
    g_canvas->fillRect(0, 0, w, 48, TFT_DARKBLUE);
    g_canvas->setTextColor(TFT_WHITE);
    g_canvas->setFont(&fonts::Font4);
    g_canvas->drawString("PaperColor", 16, 12);

    // Board info
    g_canvas->setFont(&fonts::Font2);
    g_canvas->setTextColor(TFT_BLACK);
    int y = 70;
    g_canvas->drawString("Board: M5Stack PaperColor", 16, y); y += 24;
    g_canvas->drawString("SoC:   ESP32-S3R8", 16, y); y += 24;
    g_canvas->drawString("Flash: 16MB  |  PSRAM: 8MB", 16, y); y += 24;
    g_canvas->drawString("Panel: 4\" E6 Full-Color E-Paper", 16, y); y += 24;
    g_canvas->drawString("Res:   400 x 600", 16, y); y += 30;

    // Battery
    float pct = pc_hal_battery_pct();
    bool chg  = pc_hal_is_charging();
    uint16_t mv = pc_hal_read_battery_mv();

    g_canvas->setTextColor(TFT_DARKGREEN);
    g_canvas->drawString("--- Power ---", 16, y); y += 24;
    g_canvas->setTextColor(TFT_BLACK);
    char line[64];
    snprintf(line, sizeof(line), "Battery: %.0f%%  %dmV%s",
             (double)pct, mv, chg ? "  [CHARGING]" : "");
    g_canvas->drawString(line, 16, y);

    // Battery bar
    y += 28;
    const int barW = 200, barH = 14;
    g_canvas->drawRect(16, y, barW, barH, TFT_DARKGREEN);
    int fillW = (barW - 2) * (pct > 100 ? 100 : (pct < 0 ? 0 : (int)pct)) / 100;
    if (fillW > 0) {
        g_canvas->fillRect(17, y + 1, fillW, barH - 2,
                           pct > 20 ? TFT_DARKGREEN : TFT_RED);
    }

    // Temperature
    y += 30;
    float temp = 0, hum = 0;
    if (pc_hal_read_sht40(&temp, &hum)) {
        snprintf(line, sizeof(line), "Temp: %.1f C   Hum: %.0f%%",
                 (double)temp, (double)hum);
        g_canvas->drawString(line, 16, y);
    }

    // Key pins
    y = h - 90;
    g_canvas->setTextColor(TFT_DARKGRAY);
    g_canvas->drawString("--- Key Pins ---", 16, y); y += 20;
    g_canvas->setTextColor(TFT_BLACK);
    g_canvas->drawString("BTN-A:G10  BTN-B:G9  BTN-C:G1", 16, y); y += 18;
    g_canvas->drawString("EPD SPI: CS=G44  DC=G43  BUSY=G11  RST=G12", 16, y);

    // Button hints
    g_canvas->setTextColor(TFT_DARKGRAY);
    g_canvas->setFont(&fonts::Font0);
    g_canvas->drawString("[BTN-A] Refresh  [BTN-B] Power Info  [BTN-C] Sleep",
                         16, h - 30);

    pc_hal_display();
}

void pc_hal_show_power_info(void)
{
    const int w = g_canvas->width();

    g_canvas->fillRect(0, 60, w, 140, TFT_WHITE);

    uint16_t mv = pc_hal_read_battery_mv();
    float pct   = pc_hal_battery_pct();
    bool chg    = pc_hal_is_charging();

    g_canvas->setFont(&fonts::Font2);
    g_canvas->setTextColor(TFT_BLACK);
    int y = 75;
    char line[64];

    snprintf(line, sizeof(line), "Voltage: %dmV (%.2fV)", mv, (double)mv / 1000.0);
    g_canvas->drawString(line, 20, y); y += 24;
    snprintf(line, sizeof(line), "Level:   %.0f%%", (double)pct);
    g_canvas->drawString(line, 20, y); y += 24;
    snprintf(line, sizeof(line), "Power:   %s", chg ? "USB (Charging)" : "Battery");
    g_canvas->drawString(line, 20, y); y += 24;

    snprintf(line, sizeof(line), "Standby: %.2f uA  |  Active: %.2f mA",
             (double)POWER_STANDBY_UA, (double)POWER_ACTIVE_MA);
    g_canvas->setTextColor(TFT_DARKGRAY);
    g_canvas->drawString(line, 20, y);

    pc_hal_display();
}
