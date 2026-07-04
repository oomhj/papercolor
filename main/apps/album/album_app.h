/**
 * PaperColor — Network Photo Album
 *
 * Fetches random images from API URL, displays on EPD.
 */

#pragma once

#include <cstdint>
#include <cstddef>

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

    bool fetch_image(const char* url);
    void render();
    void handle_buttons();
};
