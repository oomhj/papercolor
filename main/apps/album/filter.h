/**
 * PaperColor — Image Filters
 *
 * Pluggable filters that transform RGB565_BE decoded pixels
 * into 8-bit EPD palette indices.
 *
 * A NULL filter_fn means "no filter" — the caller should use
 * pushImage(uint16_t*) directly (M5GFX internal bit-truncation).
 */

#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Filter function: transforms a w×h RGB565_BE block into 8-bit palette indices.
 * @param src   RGB565_BE pixels, contiguous, row-major (w × h pixels)
 * @param dst   Output 8-bit palette indices (w × h bytes)
 * @param w     Width in pixels
 * @param h     Height in pixels
 */
typedef void (*filter_fn_t)(const uint16_t* src, uint8_t* dst, int w, int h);

/** A named filter entry. fn == NULL means bypass (M5GFX native). */
typedef struct {
    const char* name;
    filter_fn_t fn;
} filter_t;

/** Built-in filters. Terminated by {NULL, NULL}. */
extern const filter_t FILTERS[];

/** Number of built-in filters. */
extern const int FILTER_COUNT;

#ifdef __cplusplus
}
#endif
