/**
 * PaperColor — Hardware Abstraction Layer
 *
 * Single point of control for PaperColor hardware.
 * Inspired by M5Stack PaperColor-UserDemo HAL pattern.
 */
#pragma once

#include <cstdint>
#include <M5Unified.hpp>
#include <M5GFX.h>
#include "spi_bus.h"
#include <M5PM1.h>

#include "button.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Initialization ───────────────────────────────────────────

/**
 * @brief Initialize all PaperColor hardware.
 *
 * Init order: I2C bus recovery → M5.begin() → Canvas → PMU → power rails
 * Must be called once before any other HAL function.
 */
void pc_hal_init(void);

/**
 * @brief Periodic update — call in main loop (drives M5.update()).
 */
void pc_hal_update(void);

// ── Display ──────────────────────────────────────────────────

/**
 * @brief Reference to the EPD canvas (off-screen framebuffer).
 * Render onto this, then call hal_display() to push to EPD.
 */
extern M5Canvas* g_canvas;

/**
 * @brief Push the off-screen canvas to the EPD and trigger refresh.
 */
void pc_hal_display(void);

/**
 * @brief Push canvas to EPD AND trigger display refresh,
 *        wrapped with SPI bus claim/release for safe coexistence with SD card.
 *        Blocks until the SPI bus is available.
 * @param fast  true = epd_fastest (~500ms, ghosting possible)
 *              false = epd_quality (~1500ms, best image)
 */
void pc_hal_epd_refresh(bool fast);

// ── Power Management (M5PM1) ─────────────────────────────────

/**
 * @brief Read battery voltage in millivolts.
 * @return Voltage in mV, or 0 on error.
 */
uint16_t pc_hal_read_battery_mv(void);

/**
 * @brief Calculate battery level percentage (0–100).
 * Maps 3.0V – 4.2V linearly.
 */
float pc_hal_battery_pct(void);

/**
 * @brief Check if USB/DC power is connected (i.e., charging).
 */
bool pc_hal_is_charging(void);

/**
 * @brief Enable/disable EPD power rail.
 * EPD must be powered on before any display operation.
 */
void pc_hal_set_epd_power(bool on);

/**
 * @brief Enter deep sleep. Wakes on button or RTC alarm.
 */
void pc_hal_deep_sleep(void);

// ── Low-Power Sleep (RTC wake) ────────────────────────────────

/**
 * @brief Check if boot was from RTC alarm wake.
 * @return true if the PMU wake source was EXT_WAKE (RX8130 IRQ).
 */
bool pc_hal_is_rtc_wake(void);

/**
 * @brief Program the RX8130 RTC to wake the system after @p minutes.
 * @param minutes Time until next wake (e.g. 30 for 30min).
 * @return true on success.
 */
bool pc_hal_schedule_wake(uint32_t minutes);

/**
 * @brief Power off the entire system via M5PM1, with RTC wake scheduled.
 *        ESP32 is fully powered down.  Wake sources: RTC alarm + buttons.
 * @param minutes Minutes until next RTC wake (0 = no RTC wake, buttons only).
 */
void pc_hal_power_off_scheduled(uint32_t minutes);

/**
 * @brief Read/write one byte of RX8130 RTC battery-backed RAM (4 bytes, 0x20-0x23).
 *        Data survives deep sleep and M5PM1 power-off.
 */
bool pc_hal_rtc_ram_write(uint8_t index, uint8_t value);
bool pc_hal_rtc_ram_read(uint8_t index, uint8_t* value);

// ── Sensors ──────────────────────────────────────────────────

/**
 * @brief Read SHT40 temperature and humidity.
 * @return true on success.
 */
bool pc_hal_read_sht40(float* temp_c, float* humidity);

// ── Demo Screens ─────────────────────────────────────────────

/** @brief Show a splash/info screen on the EPD. */
void pc_hal_draw_splash(void);

/** @brief Show battery and power info on the EPD. */
void pc_hal_show_power_info(void);

#ifdef __cplusplus
}
#endif
