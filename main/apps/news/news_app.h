/**
 * PaperColor — Hot News App (P0)
 *
 * Fetches RSS headlines, displays on EPD with button navigation.
 * Uses esp_http_client for HTTP GET + simple XML parsing.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <string>

/** @brief One news item from RSS. */
struct NewsItem {
    std::string title;
    std::string desc;
    std::string source;
    std::string date;
    std::string link;
};

/** @brief News app lifecycle (matches demo app pattern). */
class NewsApp {
public:
    bool init();
    void deinit();
    void start();
    void stop();
    void update();

    /** @brief Manually trigger a network refresh. */
    void refresh();

private:
    volatile bool _running   = false;
    bool _needs_refresh      = false;
    uint32_t _last_refresh_ms = 0;
    uint32_t _last_tick_ms   = 0;

    std::vector<NewsItem> _items;
    int _current_index = 0;

    static constexpr int MAX_ITEMS = 50;
    static constexpr uint32_t AUTO_REFRESH_MS = 30 * 60 * 1000;  // 30 min

    void fetch_and_parse();
    void render();
    void handle_buttons();
};
