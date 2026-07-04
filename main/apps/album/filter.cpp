/**
 * PaperColor — Filter Implementations
 *
 * Each filter converts RGB565_BE → 8-bit palette.
 * Error diffusion dithering kernels:
 *
 *   None — simple bit truncation via M5GFX native path (fn == NULL)
 *   FS   — Floyd-Steinberg, 2×3 kernel, denominator 16
 *   JJN  — Jarvis-Judice-Ninke, 3×5 kernel, denominator 48
 */

#include "filter.h"
#include <cstdlib>
#include <cstring>
#include <cstdint>

// ── Floyd-Steinberg ──────────────────────────────────────────

static void filter_fs(const uint16_t* src, uint8_t* dst, int w, int h)
{
    int stride = w + 2;
    int* err_r = (int*)calloc(stride * 2, sizeof(int));
    int* err_g = (int*)calloc(stride * 2, sizeof(int));
    int* err_b = (int*)calloc(stride * 2, sizeof(int));
    if (!err_r || !err_g || !err_b) { free(err_r); free(err_g); free(err_b); return; }

    for (int y = 0; y < h; y++) {
        int cur = (y & 1) * stride;
        int nxt = ((y + 1) & 1) * stride;
        for (int x = 0; x < w; x++) {
            uint16_t px = __builtin_bswap16(src[y * w + x]);
            int r5 = (px >> 11) & 0x1F, g6 = (px >> 5) & 0x3F, b5 = px & 0x1F;
            r5 += err_r[cur + x + 1]; g6 += err_g[cur + x + 1]; b5 += err_b[cur + x + 1];
            if (r5 < 0) r5 = 0; else if (r5 > 31) r5 = 31;
            if (g6 < 0) g6 = 0; else if (g6 > 63) g6 = 63;
            if (b5 < 0) b5 = 0; else if (b5 > 31) b5 = 31;
            int r3 = r5 >> 2, g3 = g6 >> 3, b2 = b5 >> 3;
            int er = r5 - (r3 << 2), eg = g6 - (g3 << 3), eb = b5 - (b2 << 3);
            dst[y * w + x] = (r3 << 5) | (g3 << 2) | b2;
            err_r[cur + x + 2] += er * 7 / 16; err_r[nxt + x]     += er * 3 / 16;
            err_r[nxt + x + 1] += er * 5 / 16; err_r[nxt + x + 2] += er * 1 / 16;
            err_g[cur + x + 2] += eg * 7 / 16; err_g[nxt + x]     += eg * 3 / 16;
            err_g[nxt + x + 1] += eg * 5 / 16; err_g[nxt + x + 2] += eg * 1 / 16;
            err_b[cur + x + 2] += eb * 7 / 16; err_b[nxt + x]     += eb * 3 / 16;
            err_b[nxt + x + 1] += eb * 5 / 16; err_b[nxt + x + 2] += eb * 1 / 16;
        }
        memset(err_r + nxt, 0, stride * sizeof(int));
        memset(err_g + nxt, 0, stride * sizeof(int));
        memset(err_b + nxt, 0, stride * sizeof(int));
    }
    free(err_r); free(err_g); free(err_b);
}

// ── Jarvis-Judice-Ninke ──────────────────────────────────────

static void filter_jjn(const uint16_t* src, uint8_t* dst, int w, int h)
{
    int cols = w + 5;
    int* err_r = (int*)calloc(cols * 3, sizeof(int));
    int* err_g = (int*)calloc(cols * 3, sizeof(int));
    int* err_b = (int*)calloc(cols * 3, sizeof(int));
    if (!err_r || !err_g || !err_b) { free(err_r); free(err_g); free(err_b); return; }

    for (int y = 0; y < h; y++) {
        int r0 = (y * 3) % 3, r1 = ((y + 1) * 3) % 3, r2 = ((y + 2) * 3) % 3;
        for (int x = 0; x < w; x++) {
            uint16_t px = __builtin_bswap16(src[y * w + x]);
            int r5 = (px >> 11) & 0x1F, g6 = (px >> 5) & 0x3F, b5 = px & 0x1F;
            r5 += err_r[r0 * cols + x + 2];
            g6 += err_g[r0 * cols + x + 2];
            b5 += err_b[r0 * cols + x + 2];
            if (r5 < 0) r5 = 0; else if (r5 > 31) r5 = 31;
            if (g6 < 0) g6 = 0; else if (g6 > 63) g6 = 63;
            if (b5 < 0) b5 = 0; else if (b5 > 31) b5 = 31;
            int r3 = r5 >> 2, g3 = g6 >> 3, b2 = b5 >> 3;
            int er = r5 - (r3 << 2), eg = g6 - (g3 << 3), eb = b5 - (b2 << 3);
            dst[y * w + x] = (r3 << 5) | (g3 << 2) | b2;
            // JJN kernel /48: [[X,7,5],[3,5,7,5,3],[1,3,5,3,1]]
#define JJN(r0,r1,r2,err) do { \
    r0[x+3] += (err) * 7 /48; r0[x+4] += (err) * 5 /48; \
    r1[x+0] += (err) * 3 /48; r1[x+1] += (err) * 5 /48; r1[x+2] += (err) * 7 /48; \
    r1[x+3] += (err) * 5 /48; r1[x+4] += (err) * 3 /48; \
    r2[x+0] += (err) * 1 /48; r2[x+1] += (err) * 3 /48; r2[x+2] += (err) * 5 /48; \
    r2[x+3] += (err) * 3 /48; r2[x+4] += (err) * 1 /48; \
} while(0)
            { int *rr0 = err_r + r0*cols, *rr1 = err_r + r1*cols, *rr2 = err_r + r2*cols; JJN(rr0,rr1,rr2,er); }
            { int *rg0 = err_g + r0*cols, *rg1 = err_g + r1*cols, *rg2 = err_g + r2*cols; JJN(rg0,rg1,rg2,eg); }
            { int *rb0 = err_b + r0*cols, *rb1 = err_b + r1*cols, *rb2 = err_b + r2*cols; JJN(rb0,rb1,rb2,eb); }
#undef JJN
        }
        int clear = ((y + 3) % 3) * cols;
        memset(err_r + clear, 0, cols * sizeof(int));
        memset(err_g + clear, 0, cols * sizeof(int));
        memset(err_b + clear, 0, cols * sizeof(int));
    }
    free(err_r); free(err_g); free(err_b);
}

// ── Filter table ─────────────────────────────────────────────

const filter_t FILTERS[] = {
    { "None",               NULL        },
    { "Floyd-Steinberg",    filter_fs   },
    { "Jarvis-Judice-Ninke",filter_jjn  },
    { NULL, NULL },
};

const int FILTER_COUNT = (sizeof(FILTERS) / sizeof(FILTERS[0])) - 1;
