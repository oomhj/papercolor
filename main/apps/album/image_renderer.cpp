/**
 * PaperColor — Image Renderer Implementation
 */

#include "image_renderer.h"
#include "hal/hal.h"
#include "hal/battery.h"
#include "hal/led_driver.h"
#include "filter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_jpeg_dec.h>
#include <M5Unified.hpp>

static const char* TAG = "Render";

// ── JPEG decode ──────────────────────────────────────────────

bool ren_decode_jpeg(const uint8_t* jpeg, size_t len,
                     uint8_t** out, int* out_sw, int* out_sh,
                     int* out_crop_x, int* out_out_y)
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();

    jpeg_dec_config_t dcfg = DEFAULT_JPEG_DEC_CONFIG();
    dcfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;

    jpeg_dec_handle_t jdec = NULL;
    jpeg_dec_header_info_t hdr = {};
    jpeg_dec_io_t io = {};
    io.inbuf = (uint8_t*)jpeg;
    io.inbuf_len = (int)len;

    if (jpeg_dec_open(&dcfg, &jdec) != JPEG_ERR_OK) return false;
    bool ok_hdr = (jpeg_dec_parse_header(jdec, &io, &hdr) == JPEG_ERR_OK);
    jpeg_dec_close(jdec);
    if (!ok_hdr) return false;

    int sh = 400;
    int sw = (hdr.width * sh + hdr.height / 2) / hdr.height;
    sw = ((sw + 4) / 8) * 8;
    if (sw < w) sw = w;
    int crop_x = (sw > w) ? (sw - w) / 2 : 0;
    int out_y  = (h - sh) / 2;

    dcfg.scale.width = sw;
    dcfg.scale.height = sh;

    if (jpeg_dec_open(&dcfg, &jdec) != JPEG_ERR_OK) return false;
    jpeg_dec_parse_header(jdec, &io, &hdr);
    int out_len = 0;
    jpeg_dec_get_outbuf_len(jdec, &out_len);

    bool ok = false;
    if (out_len > 0) {
        uint8_t* buf = (uint8_t*)jpeg_calloc_align(out_len, 16);
        if (buf) {
            io.outbuf = buf; io.out_size = out_len;
            if (jpeg_dec_process(jdec, &io) == JPEG_ERR_OK) {
                *out = buf; *out_sw = sw; *out_sh = sh;
                *out_crop_x = crop_x; *out_out_y = out_y;
                ok = true;
            } else { jpeg_free_align(buf); }
        }
    }
    jpeg_dec_close(jdec);
    return ok;
}

// ── Battery icon overlay ────────────────────────────────────

static void draw_battery_icon(void)
{
    const int w = M5.Display.width();
    int pct = bat_get_pct();
    int segs = (pct + 9) / 20;
    if (segs > 5) segs = 5;

    int bx = w - 36, by = 6;
    int bw = 30, bh = 13;

    g_canvas->drawRect(bx, by, bw, bh, TFT_BLACK);
    g_canvas->fillRect(bx + bw, by + 3, 3, bh - 6, TFT_BLACK);

    int seg_w = 4, seg_gap = 1, seg_h = 9;
    int sx = bx + 3, sy = by + 2;
    for (int i = 0; i < 5; i++) {
        int x = sx + i * (seg_w + seg_gap);
        if (i < segs)
            g_canvas->fillRect(x, sy, seg_w, seg_h, TFT_BLACK);
        else
            g_canvas->drawRect(x, sy, seg_w, seg_h, TFT_BLACK);
    }

    if (bat_is_charging()) {
        g_canvas->setTextColor(TFT_BLACK);
        g_canvas->setFont(&fonts::Font0);
        g_canvas->drawString("+", bx + 11, by);
    }
}

// ── Dither + display + EPD refresh ───────────────────────────

void ren_render(uint8_t* decoded, int sw, int sh,
                int crop_x, int out_y, bool fast)
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    const uint16_t* src = (const uint16_t*)decoded + out_y * sw + crop_x;

    // Allocate working buffers
    uint8_t* dither = (uint8_t*)malloc(w * h);
    if (!dither) return;
    uint16_t* crop = (uint16_t*)malloc(w * h * 2);
    if (!crop) { free(dither); return; }

    // Crop center region
    for (int y = 0; y < h; y++)
        memcpy(crop + y * w, src + y * sw, w * 2);

    // Floyd-Steinberg dither
    uint32_t t0 = esp_timer_get_time() / 1000;
    FILTERS[1].fn(crop, dither, w, h);
    g_canvas->fillScreen(TFT_WHITE);
    g_canvas->pushImage(0, 0, w, h, dither);
    draw_battery_icon();
    uint32_t t1 = esp_timer_get_time() / 1000;

    free(crop);
    free(dither);

    // LED flash before EPD refresh — user sees EPD updating visually
    led_async_flash(0, 255, 0, 4);

    // EPD refresh
    pc_hal_epd_refresh(fast);
    uint32_t t2 = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "filter %dms epd %dms%s",
             (int)(t1 - t0), (int)(t2 - t1), fast ? " fast" : "");
}
