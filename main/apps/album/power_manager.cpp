/**
 * PaperColor — Power Manager Implementation
 */

#include "power_manager.h"
#include "hal/hal.h"
#include "hal/sd_card.h"
#include "hal/led_driver.h"
#include "hal/battery.h"
#include <cstdio>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <M5Unified.hpp>

static const char* TAG = "Power";

// ── Idle tracking ────────────────────────────────────────────

static uint64_t s_last_activity = 0;

void PowerManager::mark_activity()
{
    s_last_activity = esp_timer_get_time() / 1000;
}

bool PowerManager::is_idle()
{
    uint64_t now = esp_timer_get_time() / 1000;
    return (now >= PM_IDLE_SLEEP_MS && now - s_last_activity >= PM_IDLE_SLEEP_MS);
}

// ── Deep sleep ───────────────────────────────────────────────

void PowerManager::go_to_sleep(int idx, int date, bool sd_mounted)
{
    // Turn off LED (SK6812 holds last color in registers during sleep)
    led_before_sleep();

    // Low battery → permanent shutdown
    if (bat_is_low()) {
        ESP_LOGW(TAG, "Battery low (%u%%), shutting down permanently", bat_get_pct());
        save_rtc_ram(idx, date);
        if (sd_mounted) { sd_card_unmount(); }
        pc_hal_deep_sleep();  // no timer wake
        return;
    }

    ESP_LOGI(TAG, "Deep sleep (%u%%), idx=%d, wake in 30 min or button",
             bat_get_pct(), idx);

    // Save state
    save_rtc_ram(idx, date);
    if (sd_mounted) { sd_card_unmount(); }

    // Enter sleep
    M5.Display.sleep();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(PM_WAKE_INTERVAL * 1000000ULL);  // 30 min
    esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 1) | (1ULL << 9) | (1ULL << 10),
                                  ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

// ── RTC RAM ──────────────────────────────────────────────────

bool PowerManager::load_rtc_ram(int* idx, int* date)
{
    uint8_t i = 0, lo = 0, hi = 0;
    bool ok = pc_hal_rtc_ram_read(0, &i);
    pc_hal_rtc_ram_read(1, &lo);
    pc_hal_rtc_ram_read(2, &hi);
    if (ok && i >= 1 && i <= 10) *idx = i;
    *date = (hi << 8) | lo;
    return ok;
}

void PowerManager::save_rtc_ram(int idx, int date)
{
    pc_hal_rtc_ram_write(0, (uint8_t)idx);
    pc_hal_rtc_ram_write(1, (uint8_t)(date & 0xFF));
    pc_hal_rtc_ram_write(2, (uint8_t)((date >> 8) & 0xFF));
}
