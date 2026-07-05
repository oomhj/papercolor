/**
 * PaperColor — Battery Monitor Implementation
 */

#include "battery.h"
#include "config.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <M5PM1.h>

static const char* TAG = "BAT";

extern M5PM1* s_pmu;  // shared from hal.cpp

// ── Cached state ────────────────────────────────────────────

static uint16_t s_last_mv     = 0;
static uint8_t  s_last_pct    = 0;
static bool     s_last_chg    = false;
static uint64_t s_last_read_ms = 0;
static bool     s_cache_valid  = false;

#define READ_INTERVAL_MS  (30ULL * 1000)  // read from PMU every 30s

// ── Internal read ───────────────────────────────────────────

static void read_battery(void)
{
    if (!s_pmu) return;

    uint16_t mv = 0;
    if (s_pmu->readVbat(&mv) != M5PM1_OK) return;
    s_last_mv = mv;

    // Percentage (linear map 3.0V–4.2V)
    uint16_t empty_mv = (uint16_t)(BATTERY_EMPTY_V * 1000);
    uint16_t full_mv  = (uint16_t)(BATTERY_FULL_V  * 1000);
    if (mv >= full_mv)       s_last_pct = 100;
    else if (mv <= empty_mv) s_last_pct = 0;
    else s_last_pct = (uint8_t)((uint32_t)(mv - empty_mv) * 100U / (full_mv - empty_mv));

    // Charging
    m5pm1_pwr_src_t src = M5PM1_PWR_SRC_UNKNOWN;
    s_last_chg = (s_pmu->getPowerSource(&src) == M5PM1_OK &&
                  (src == M5PM1_PWR_SRC_5VIN || src == M5PM1_PWR_SRC_5VINOUT));

    s_last_read_ms = esp_timer_get_time() / 1000;
    s_cache_valid = true;
}

// ── Public API ──────────────────────────────────────────────

uint8_t bat_get_pct(void)
{
    uint64_t now = esp_timer_get_time() / 1000;
    if (!s_cache_valid || now - s_last_read_ms >= READ_INTERVAL_MS)
        read_battery();
    return s_last_pct;
}

uint16_t bat_get_mv(void)
{
    uint64_t now = esp_timer_get_time() / 1000;
    if (!s_cache_valid || now - s_last_read_ms >= READ_INTERVAL_MS)
        read_battery();
    return s_last_mv;
}

bool bat_is_charging(void)
{
    uint64_t now = esp_timer_get_time() / 1000;
    if (!s_cache_valid || now - s_last_read_ms >= READ_INTERVAL_MS)
        read_battery();
    return s_last_chg;
}

void bat_update(void)
{
    read_battery();  // cached at 30s internally
    ESP_LOGI(TAG, "%u%% %umV%s", s_last_pct, s_last_mv, s_last_chg ? " CHG" : "");
}

bool bat_is_low(void)
{
    uint8_t pct = bat_get_pct();
    uint16_t mv = bat_get_mv();
    return (pct < 10) || (mv < 3300);
}
