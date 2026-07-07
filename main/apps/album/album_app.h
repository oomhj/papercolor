/**
 * PaperColor — Daily Photo Slideshow
 *
 * Lifecycle orchestrator.  Delegates slideshow logic to SlideShow,
 * power management to PowerManager, downloads to ImageDownloader,
 * rendering to ImageRenderer.
 */

#pragma once

#include <cstdint>
#include "slide_show.h"
#include "power_manager.h"

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

    SlideShow     _slideshow;
    PowerManager  _pm;
    uint64_t      _btn_busy_until = 0;

    // SD
    bool _sd_mounted         = false;

    void handle_buttons(void);
};
