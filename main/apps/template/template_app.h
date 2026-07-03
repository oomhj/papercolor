/**
 * PaperColor — Template App
 *
 * Reference pattern for creating PaperColor applications.
 * Follows the same lifecycle as apps in the m5_demo project:
 *   init() → start() → [update() loop] → stop() → deinit()
 */
#pragma once

#include <cstdint>

class TemplateApp {
public:
    /** @brief Initialize resources. */
    bool init();

    /** @brief Release resources. */
    void deinit();

    /** @brief Start the app (renders initial screen). */
    void start();

    /** @brief Stop the app. */
    void stop();

    /** @brief Periodic update — call from main loop. */
    void update();

private:
    volatile bool _running   = false;
    uint32_t _last_tick_ms   = 0;

    void render();
    void handle_buttons();
};
