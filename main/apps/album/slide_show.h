/**
 * PaperColor — SlideShow Controller
 *
 * Manages slide index, date tracking, auto-advance, daily update.
 * Coordinates ImageDownloader + ImageRenderer for the complete
 * load → decode → display pipeline.
 *
 * AlbumApp owns one SlideShow instance and delegates to it.
 */

#pragma once

#include <cstdint>
#include <cstddef>

// Max images in the album
#define SS_MAX_IMAGES  10

class SlideShow {
public:
    // ── State (public for AlbumApp access) ────────────────────
    int   current_idx       = 1;    // 1..SS_MAX_IMAGES
    int   total_images      = 0;    // 0 = not ready
    int   last_update_date  = 0;    // YYYYMMDD
    bool  dl_pending        = false;
    bool  dl_in_progress    = false;

    // ── Lifecycle ─────────────────────────────────────────────
    void init(int today);
    void deinit();

    // ── Navigation ────────────────────────────────────────────
    void show_next();
    void show_prev();
    void check_auto_advance();
    void check_daily_update(int today);

    // ── Load & display ────────────────────────────────────────
    bool load_and_show(int index, bool fast = false);
    bool fetch_and_show_one();

    // ── Download ──────────────────────────────────────────────
    void refresh_all_images();
    void run_pending_download();

public:
    uint64_t _last_slide_ms      = 0;
    uint64_t _last_date_check_ms = 0;

    // Buffer for current image
    uint8_t* _img_buf     = nullptr;
    size_t   _img_len     = 0;
    uint8_t* _decoded_buf = nullptr;
    int _decoded_sw       = 0;
    int _decoded_sh       = 0;
    int _decoded_crop_x   = 0;
    int _decoded_out_y    = 0;

    // Helpers
    int  get_today(void);
    int  read_index_date(void);
    void write_index_date(int date);
    bool ensure_album_folder(void);
    int  scan_folder_images(void);
    bool download_one(int index);
    bool decode_and_render(const uint8_t* jpeg, size_t len, bool fast = false);
};
