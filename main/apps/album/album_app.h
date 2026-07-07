/**
 * PaperColor — Daily Photo Slideshow
 *
 * Stores 10 images in /sd/album/, updates daily via WiFi.
 * Falls back to existing images if no network.
 * index.txt records last update date (YYYYMMDD).
 * TOP long press forces manual update.
 *
 * On boot: shows cached image immediately, then downloads new ones.
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

    // Slideshow state
    int   _current_idx       = 1;    // 1..10
    int   _total_images      = 0;    // 0 = not ready
    uint64_t _last_slide_ms  = 0;

    // Daily update tracking
    int  _last_update_date   = 0;    // from index.txt (YYYYMMDD)
    uint64_t _last_date_check_ms = 0;

    // Deferred download
    bool  _dl_pending        = false; // download queued, will run in update()
    bool  _dl_in_progress    = false; // download currently happening
    uint64_t _btn_busy_until  = 0;     // button debounce deadline (ms)

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
    void refresh_all_images(void);   // unified: del → dl 1 → show → queue rest
    bool download_one(int index);
    bool fetch_and_show_one(void);   // no-SD: fetch 1, show immediately

    // Display
    bool load_and_show(int index, bool fast = false);
    bool decode_and_render(const uint8_t* jpeg, size_t len, bool fast = false);

    // Navigation
    void show_next(void);
    void show_prev(void);
    void check_auto_advance(void);
    void check_daily_update(void);
    void run_pending_download(void);

    void go_to_sleep(void);
    void handle_buttons(void);
};
