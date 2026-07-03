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

    uint8_t* _img_buf       = nullptr;  // JPEG data buffer
    size_t _img_len         = 0;

    bool fetch_image(const char* url);
    void render();
    void handle_buttons();
};
