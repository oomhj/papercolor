/**
 * PaperColor — Network Photo Album
 *
 * Fetches random images from API URL, displays on EPD.
 * Decoded images are cached on SD card for instant display
 * on subsequent boots (no network/decode needed).
 */

#pragma once

#include <cstdint>
#include <cstddef>

// Cache file on SD card — holds the last successfully decoded RGB565_BE buffer
// with a header so it can be loaded instantly on next boot.
#define ALBUM_SD_CACHE "/sd/ALBUM.BIN"

class AlbumApp {
public:
    bool init();
    void deinit();
    void start();
    void stop();
    void update();
    void refresh();

private:
    volatile bool _running  = false;
    bool _needs_refresh     = false;
    bool _needs_rerender    = false;

    uint8_t* _img_buf       = nullptr;  // JPEG data buffer (freed after decode)
    size_t _img_len         = 0;

    // Decoded RGB565_BE buffer (cached for filter switching)
    uint8_t* _decoded_buf   = nullptr;
    int _decoded_sw         = 0;   // scaled width
    int _decoded_sh         = 0;   // scaled height
    int _decoded_crop_x     = 0;   // center crop x offset
    int _decoded_out_y      = 0;   // vertical centering y offset

    int _filter_idx         = 0;   // current filter index

    bool _sd_mounted        = false;

    // SD cache I/O
    bool cache_save(void);
    bool cache_load(void);

    bool fetch_image(const char* url);
    void render();
    void handle_buttons();
};
