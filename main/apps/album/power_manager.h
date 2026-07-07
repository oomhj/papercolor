/**
 * PaperColor — Power Manager
 *
 * Idle tracking, deep sleep, RTC RAM persistence.
 * Used by AlbumApp to manage low-power sleep cycles.
 */

#pragma once

#include <cstdint>

#define PM_IDLE_SLEEP_MS  (60ULL * 1000)  // 60s idle → deep sleep
#define PM_WAKE_INTERVAL  (30 * 60)        // 30 min RTC wake

class PowerManager {
public:
    /** Reset idle timer (call on user activity). */
    void mark_activity();

    /** True if idle timeout has elapsed since last mark_activity(). */
    bool is_idle();

    /**
     * Enter deep sleep.  Saves state to RTC RAM, unmounts SD,
     * sets RTC timer + button wake.
     * @param idx         Current slide index (saved to RTC RAM)
     * @param date        Last update date (saved to RTC RAM)
     * @param sd_mounted  If true, unmount SD before sleep
     */
    void go_to_sleep(int idx, int date, bool sd_mounted);

    // ── Static helpers for init path ──────────────────────────

    /** Read index + date from RTC battery-backed RAM. */
    static bool load_rtc_ram(int* idx, int* date);

    /** Write index + date to RTC battery-backed RAM. */
    static void save_rtc_ram(int idx, int date);
};
