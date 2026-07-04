/**
 * PaperColor — LED Driver
 *
 * Dual-mode: synchronous (blocking) and asynchronous (dedicated FreeRTOS task).
 * Async mode allows breathing/flashing without blocking the caller.
 *
 * Usage:
 *   led_async_start();           // create LED task once at boot
 *   led_async_breath(0,0,255,800); // non-blocking, returns immediately
 *
 * For direct synchronous control (no task needed):
 *   led_set_color(r, g, b);
 */

#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// ── Init ────────────────────────────────────────────────────────

/** @brief Initialize LED driver (hardware only). Call once at boot. */
void led_init(void);

/**
 * @brief  Start the async LED task (creates a FreeRTOS task + queue).
 *         Non-blocking led_async_*() calls require this.
 * @note   Calling led_async_*() without starting the task is a no-op.
 */
void led_async_start(void);

// ── Synchronous API (blocking, no task needed) ──────────────────

void led_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_set_single(int index, uint8_t r, uint8_t g, uint8_t b);
void led_set_brightness(uint8_t brightness);
void led_flush(void);
void led_off(void);
void led_breath(uint8_t r, uint8_t g, uint8_t b, int duration_ms);
void led_breath_forever(uint8_t r, uint8_t g, uint8_t b);
void led_flash(uint8_t r, uint8_t g, uint8_t b, int count);
void led_flash_forever(uint8_t r, uint8_t g, uint8_t b);

// ── Asynchronous API (non-blocking, requires led_async_start()) ──

/** @brief Set color immediately. Overrides any ongoing effect. */
void led_async_color(uint8_t r, uint8_t g, uint8_t b);

/** @brief Breath once. Queued, returns immediately. */
void led_async_breath(uint8_t r, uint8_t g, uint8_t b, int duration_ms);

/** @brief Breath continuously until led_async_stop(). */
void led_async_breath_forever(uint8_t r, uint8_t g, uint8_t b);

/** @brief Flash N times. Queued, returns immediately. */
void led_async_flash(uint8_t r, uint8_t g, uint8_t b, int count);

/** @brief Flash continuously until led_async_stop(). */
void led_async_flash_forever(uint8_t r, uint8_t g, uint8_t b);

/** @brief Turn off immediately. Clears queue, stops ongoing effects. */
void led_async_off(void);

/** @brief Stop all effects and turn off. Alias for led_async_off(). */
void led_async_stop(void);

#ifdef __cplusplus
}
#endif
