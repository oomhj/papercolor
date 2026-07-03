/**
 * PaperColor — Hot News App Implementation (P0)
 *
 * Lifecycle: init → start → [update loop] → stop → deinit
 * Fetches RSS headlines, displays on EPD, button navigation.
 */
#include "news_app.h"
#include "news_fetcher.h"
#include "news_parser.h"
#include "hal/hal.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>

static const char* TAG = "NewsApp";

// RSS feed URL — 少数派 sspai
static const char* RSS_URLS[] = {
    "https://sspai.com/feed",
};
static constexpr int RSS_URL_COUNT = sizeof(RSS_URLS) / sizeof(RSS_URLS[0]);

// ── WiFi credentials (edit for your network) ──────────────────
#ifndef NEWS_SSID
#define NEWS_SSID "Jason-home"
#endif
#ifndef NEWS_PASS
#define NEWS_PASS "admin1234"
#endif

// ── WiFi STA connect helper ──────────────────────────────────

static bool wifi_connect_sta()
{
    if (strcmp(NEWS_SSID, "YOUR_WIFI_SSID") == 0) {
        ESP_LOGE(TAG, "WiFi not configured!");
        return false;
    }

    ESP_LOGI(TAG, "Connecting WiFi: %s", NEWS_SSID);

    // NVS init (required for WiFi)
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) { return false; }

    // WiFi init — safe, ignore already-inited
    if (esp_netif_init() != ESP_OK && esp_netif_init() != ESP_ERR_INVALID_STATE) return false;
    if (esp_event_loop_create_default() != ESP_OK && esp_event_loop_create_default() != ESP_ERR_INVALID_STATE) return false;
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) return false;

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&wcfg);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;

    // Configure and start
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wc = {};
    memcpy(wc.sta.ssid, NEWS_SSID, strlen(NEWS_SSID));
    if (strlen(NEWS_PASS)) memcpy(wc.sta.password, NEWS_PASS, strlen(NEWS_PASS));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    esp_wifi_connect();

    // Poll 25s
    uint32_t deadline = (uint32_t)(esp_timer_get_time() / 1000ULL) + 25000;
    while ((uint32_t)(esp_timer_get_time() / 1000ULL) < deadline) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi OK: %s, RSSI=%d", NEWS_SSID, ap.rssi);
            // (DNS is handled by DHCP)
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    ESP_LOGW(TAG, "WiFi connect timeout");
    return false;
}

// ── Lifecycle ────────────────────────────────────────────────

bool NewsApp::init()
{
    ESP_LOGI(TAG, "init");
    _items.clear();
    _current_index = 0;
    _needs_refresh = true;
    return true;
}

void NewsApp::deinit()
{
    ESP_LOGI(TAG, "deinit");
    _running = false;
    _items.clear();
    _items.shrink_to_fit();
}

void NewsApp::start()
{
    ESP_LOGI(TAG, "start");
    _running         = true;
    _last_tick_ms    = (uint32_t)(esp_timer_get_time() / 1000ULL);
    _needs_refresh   = true;
}

void NewsApp::stop()
{
    ESP_LOGI(TAG, "stop");
    _running = false;
}

void NewsApp::refresh()
{
    _needs_refresh = true;
}

// ── Update loop ──────────────────────────────────────────────

void NewsApp::update()
{
    if (!_running) return;

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Periodic auto-refresh (30 min)
    if (_last_refresh_ms > 0 && now - _last_refresh_ms >= AUTO_REFRESH_MS) {
        _needs_refresh = true;
    }

    // Fetch new data
    if (_needs_refresh) {
        _needs_refresh = false;
        fetch_and_parse();
        _last_refresh_ms = now;
    }

    handle_buttons();

    // Initial render once items loaded
    static bool first_render = true;
    if (first_render && !_items.empty()) {
        first_render = false;
        render();
    }
}

// ── Fetch & Parse ────────────────────────────────────────────

// ── LED helper ───────────────────────────────────────────────
static void led_set(uint8_t r, uint8_t g, uint8_t b) {
    M5.Led.setBrightness(60);
    M5.Led.setAllColor(r, g, b);
    M5.Led.display();
}
static void led_off() {
    M5.Led.setBrightness(0);
    M5.Led.display();
}

void NewsApp::fetch_and_parse()
{
    ESP_LOGI(TAG, "Fetching RSS (%d sources)", RSS_URL_COUNT);

    // LED: blue = connecting
    led_set(0, 0, 255);

    // ── WiFi ──
    bool wifi_ok = wifi_connect_sta();

    // ── HTTP fetch ──
    fetch_result_t res = {nullptr, 0, ESP_FAIL};
    if (wifi_ok) {
        for (int i = 0; i < RSS_URL_COUNT; i++) {
            ESP_LOGI(TAG, "Trying RSS[%d]: %s", i, RSS_URLS[i]);
            res = news_fetch_url(RSS_URLS[i], 15000);
            if (res.err == ESP_OK && res.data && res.data[0]) break;
            if (res.data) { free(res.data); res.data = nullptr; }
        }
    }

    // ── 结果处理 ──
    if (res.err == ESP_OK && res.data && res.data[0]) {
        auto new_items = news_parse_rss(res.data, MAX_ITEMS);
        free(res.data);
        if (!new_items.empty()) {
            _items = std::move(new_items);
            _current_index = 0;
            ESP_LOGI(TAG, "Loaded %zu items", _items.size());
            led_set(0, 255, 0);      // LED: green = success
            vTaskDelay(pdMS_TO_TICKS(600));
            led_off();
            render();                // EPD: 只刷一次内容
            return;
        }
    }

    // ── 失败 ──
    if (_items.empty()) {
        led_set(255, 0, 0);          // LED: red = failed
        vTaskDelay(pdMS_TO_TICKS(2000));
        led_off();
    }
}

// ── Rendering ────────────────────────────────────────────────

void NewsApp::render()
{
    if (_items.empty()) {
        g_canvas->fillScreen(TFT_WHITE);
        g_canvas->setTextColor(TFT_DARKGRAY);
        g_canvas->setFont(&fonts::Font4);
        g_canvas->drawString("No news available", 20, 100);
        g_canvas->pushSprite(0, 0);
        M5.Display.display();
        return;
    }

    const int w = g_canvas->width();
    const int h = g_canvas->height();

    if (_current_index < 0) _current_index = 0;
    if (_current_index >= (int)_items.size()) _current_index = _items.size() - 1;

    const auto& item = _items[_current_index];

    M5.Display.setEpdMode(epd_mode_t::epd_fastest);
    g_canvas->fillScreen(TFT_WHITE);

    // ── Header ──
    g_canvas->fillRect(0, 0, w, 36, 0x2118);
    g_canvas->setTextColor(TFT_WHITE);
    g_canvas->setFont(&fonts::Font2);

    char buf[80];
    int today_y = 0, today_m = 0, today_d = 0;
    m5::rtc_date_t rd;
    m5::rtc_time_t rt;
    if (M5.Rtc.getDateTime(&rd, &rt)) {
        today_y = rd.year;
        if (today_y < 100) today_y += 2000;
        today_m = rd.month;
        today_d = rd.date;
    }
    snprintf(buf, sizeof(buf), "Hot News   %04d-%02d-%02d",
             today_y, today_m, today_d);
    g_canvas->drawString(buf, 12, 10);

    snprintf(buf, sizeof(buf), "%d/%zu",
             _current_index + 1, _items.size());
    g_canvas->drawString(buf, w - 60, 10);

    // ── Title ──
    g_canvas->setTextColor(TFT_BLACK);
    g_canvas->setFont(&fonts::Font6);
    g_canvas->drawString(item.title.c_str(), 20, 60);

    // ── Source & Date ──
    g_canvas->setFont(&fonts::Font2);
    g_canvas->setTextColor(TFT_DARKGRAY);
    if (!item.source.empty() || !item.date.empty()) {
        snprintf(buf, sizeof(buf), "%s%s%s",
                 item.source.c_str(),
                 (!item.source.empty() && !item.date.empty()) ? "  |  " : "",
                 item.date.c_str());
        g_canvas->drawString(buf, 20, 130);
    }

    // ── Description ──
    if (!item.desc.empty()) {
        g_canvas->setFont(&fonts::Font2);
        g_canvas->setTextColor(TFT_BLACK);
        const std::string& d = item.desc;
        size_t pos = 0;
        int line_h = g_canvas->fontHeight() + 4;
        int ty = 180;
        int max_lines = 5;
        int lines = 0;

        while (pos < d.length() && lines < max_lines) {
            size_t end = pos + 50;
            if (end > d.length()) end = d.length();
            if (end < d.length()) {
                size_t space = d.rfind(' ', end);
                if (space > pos && space != std::string::npos) end = space;
            }
            g_canvas->drawString(d.substr(pos, end - pos).c_str(), 20, ty);
            ty += line_h;
            pos = end;
            while (pos < d.length() && d[pos] == ' ') pos++;
            lines++;
        }
    }

    // ── Footer ──
    g_canvas->setTextColor(TFT_DARKGRAY);
    g_canvas->setFont(&fonts::Font0);
    snprintf(buf, sizeof(buf), "[A] << Prev    [B] Refresh    [C] Next >>");
    g_canvas->drawString(buf, 12, h - 16);

    g_canvas->pushSprite(0, 0);
    M5.Display.display();

    ESP_LOGI(TAG, "Displayed [%d/%zu]: %s",
             _current_index + 1, _items.size(), item.title.c_str());
}

// ── Buttons ──────────────────────────────────────────────────

void NewsApp::handle_buttons()
{
    if (_items.empty()) return;

    bool changed = false;

    if (BTN_DOWN.wasClicked()) {
        if (_current_index > 0) { _current_index--; changed = true; }
    }

    if (BTN_TOP.wasClicked()) {
        if (_current_index < (int)_items.size() - 1) { _current_index++; changed = true; }
    }

    if (BTN_UP.wasClicked()) {
        ESP_LOGI(TAG, "Manual refresh");
        _needs_refresh = true;
    }

    if (changed) render();
}
