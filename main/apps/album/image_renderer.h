/**
 * PaperColor — Image Renderer
 *
 * JPEG decode → Floyd-Steinberg dither → EPD display.
 * Battery icon overlay.  Standalone module used by SlideShow.
 */

#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief Decode a JPEG into a RGB565 framebuffer with aspect-fit scaling.
 *        Caller must free(*out) via jpeg_free_align().
 * @return true on success.
 */
bool ren_decode_jpeg(const uint8_t* jpeg, size_t len,
                     uint8_t** out, int* out_sw, int* out_sh,
                     int* out_crop_x, int* out_out_y);

/**
 * @brief Dither (Floyd-Steinberg) and push the decoded image to the
 *        EPD canvas, overlay battery icon, and trigger EPD refresh.
 * @param decoded   RGB565 buffer from ren_decode_jpeg
 * @param sw,sh     scaled image dimensions
 * @param crop_x    X offset into scaled image (center crop)
 * @param out_y     Y offset on target display
 * @param fast      true = epd_fastest, false = epd_quality
 */
void ren_render(uint8_t* decoded, int sw, int sh,
                int crop_x, int out_y, bool fast);
