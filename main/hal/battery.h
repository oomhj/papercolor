/**
 * PaperColor — Battery Monitor Module
 *
 * Wraps M5PM1 battery reads with caching and periodic serial logging.
 * Reuses the global s_pmu from hal.cpp instead of creating new instances.
 */

#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Get battery level 0–100%. */
uint8_t bat_get_pct(void);

/** @brief Get battery voltage in mV. */
uint16_t bat_get_mv(void);

/** @brief True if USB/DC power is connected. */
bool bat_is_charging(void);

/**
 * @brief Read and log battery status.
 *        Call periodically from main loop (e.g. every 60s).
 *        Prints: [BAT] 75% 4080mV CHG
 *        Skips logging if called more often than the sample interval.
 */
void bat_update(void);

/** @brief True if voltage < 3300mV or percentage < 10%. */
bool bat_is_low(void);

#ifdef __cplusplus
}
#endif
