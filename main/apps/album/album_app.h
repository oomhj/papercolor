/**
 * PaperColor — Daily Photo Slideshow
 *
 * Stores 10 images in /sd/album/, updates daily via WiFi.
 * Falls back to existing images if no network.
 * index.txt records last update date (YYYYMMDD).
 * TOP long press forces manual update.
 */

#pragma once

#include <cstdint>
#include <cstddef>

#define ALBUM_DIR    "/sd/album"
#define ALBUM_MAX_IMAGES  10

class AlbumApp {
public:
    bool init();
    void deinit();
    void start();
    void stop();
    void update();
    void refresh();

private:
    volatile bool _running   = false;
    bool _needs_refresh      = false;

    // Slideshow state
    int   _current_idx       = 1;    // 1..10
    int   _total_images      = 0;    // 0 = not ready
    uint64_t _last_slide_ms  = 0;

    // Daily update tracking
    int  _last_update_date   = 0;    // from index.txt (YYYYMMDD)
    uint64_t _last_date_check_ms = 0;

    // JPEG + decoded buffer
    uint8_t* _img_buf        = nullptr;
    size_t   _img_len        = 0;
    uint8_t* _decoded_buf    = nullptr;
    int _decoded_sw          = 0;
    int _decoded_sh          = 0;
    int _decoded_crop_x      = 0;
    int _decoded_out_y       = 0;

    int _filter_idx          = 1;

    // SD
    bool _sd_mounted         = false;

    // ── Helpers ──

    // Folder
    bool ensure_album_folder(void);
    int  scan_folder_images(void);
    int  read_index_date(void);
    void write_index_date(int date);
    int  get_today(void);

    // Download
    bool update_images(void);
    bool download_one(int index);
    bool fetch_and_show_one(void);   // no-SD: fetch 1, show immediately

    // Display
    bool load_and_show(int index);
    bool decode_and_render(const uint8_t* jpeg, size_t len);

    // Navigation
    void show_next(void);
    void show_prev(void);
    void check_auto_advance(void);
    void check_daily_update(void);

    void handle_buttons(void);
};
