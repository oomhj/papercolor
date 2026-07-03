/**
 * PaperColor — Network Photo Album
 *
 * Fetches random scenery images from API, displays on EPD.
 * BTN-A/C: new random image  |  BTN-B: reload  |  BTN-C hold: sleep
 */
#include "album_app.h"
#include "hal/hal.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <nvs_flash.h>

static const char* TAG = "Album";

// ── Image API ─────────────────────────────────────────────────
// Using IP to bypass DNS issues, Host for TLS SNI.
static const char* IMAGE_HOST = "tu.ltyuanfang.cn";
static const char* IMAGE_URL  = "https://192.140.186.120/api/fengjing.php";

// ── WiFi ──────────────────────────────────────────────────────
#ifndef ALBUM_SSID
#define ALBUM_SSID "Jason-home"
#endif
#ifndef ALBUM_PASS
#define ALBUM_PASS "admin1234"
#endif

static bool wifi_connect()
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND)
        { nvs_flash_erase(); nvs_flash_init(); }
    if (esp_netif_init() != ESP_OK && esp_netif_init() != ESP_ERR_INVALID_STATE) return false;
    if (esp_event_loop_create_default() != ESP_OK && esp_event_loop_create_default() != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wcfg) != ESP_OK && esp_wifi_init(&wcfg) != ESP_ERR_INVALID_STATE) return false;

    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t wc = {};
    strcpy((char*)wc.sta.ssid, ALBUM_SSID);
    strcpy((char*)wc.sta.password, ALBUM_PASS);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
    esp_wifi_connect();

    uint32_t deadline = esp_timer_get_time() / 1000 + 25000;
    while (esp_timer_get_time() / 1000 < deadline) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return true;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

// ── HTTP fetch (bypass DNS, use IP + Host header) ─────────────

static bool http_fetch_image(const char* ip_url, const char* host,
                              uint8_t** out_buf, size_t* out_len)
{
    *out_buf = nullptr; *out_len = 0;

    esp_http_client_config_t cfg = {};
    cfg.url                       = ip_url;
    cfg.host                      = host;     // TLS SNI
    cfg.timeout_ms                = 30000;
    cfg.buffer_size               = 8192;
    cfg.crt_bundle_attach         = esp_crt_bundle_attach;
    cfg.method                    = HTTP_METHOD_GET;
    cfg.max_redirection_count     = 5;        // follow 302

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;

    esp_err_t err = esp_http_client_perform(c);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform: %s", esp_err_to_name(err));
        esp_http_client_cleanup(c);
        return false;
    }

    int status = esp_http_client_get_status_code(c);
    int64_t len = esp_http_client_get_content_length(c);
    char url_buf[256] = {0};
    esp_http_client_get_url(c, url_buf, sizeof(url_buf));
    ESP_LOGI(TAG, "HTTP %d, len=%lld, url=%s", status, (long long)len, url_buf);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d", status);
        esp_http_client_cleanup(c);
        return false;
    }

    size_t alloc = (len > 0) ? (size_t)len + 1 : 65536;
    uint8_t* buf = (uint8_t*)malloc(alloc);
    if (!buf) { esp_http_client_cleanup(c); return false; }

    size_t total = 0;
    int rd;
    while ((rd = esp_http_client_read(c, (char*)buf + total, alloc - total - 1)) > 0) {
        total += rd;
        if (total + 1024 >= alloc) {
            alloc *= 2;
            uint8_t* nb = (uint8_t*)realloc(buf, alloc);
            if (!nb) { free(buf); esp_http_client_cleanup(c); return false; }
            buf = nb;
        }
    }
    buf[total] = '\0';

    ESP_LOGI(TAG, "Fetched %zu bytes", total);
    esp_http_client_cleanup(c);
    *out_buf = buf;
    *out_len = total;
    return true;
}

// ── Lifecycle ────────────────────────────────────────────────

bool AlbumApp::init() { _img_buf = nullptr; _img_len = 0; return true; }

void AlbumApp::deinit()
{
    _running = false;
    if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
}

void AlbumApp::start() { _running = true; _needs_refresh = true; }
void AlbumApp::stop()  { _running = false; }
void AlbumApp::refresh() { _needs_refresh = true; }

void AlbumApp::update()
{
    if (!_running) return;

    if (_needs_refresh) {
        _needs_refresh = false;

        M5.Led.setBrightness(60);
        M5.Led.setAllColor(0, 0, 255);
        M5.Led.display();

        bool ok = false;
        if (wifi_connect()) {
            if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
            ok = http_fetch_image(IMAGE_URL, IMAGE_HOST, &_img_buf, &_img_len);
        }

        if (ok) {
            M5.Led.setAllColor(0, 255, 0);
            M5.Led.display();
            vTaskDelay(pdMS_TO_TICKS(400));
        } else {
            M5.Led.setAllColor(255, 0, 0);
            M5.Led.display();
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        M5.Led.setBrightness(0);
        M5.Led.display();

        render();
    }
    handle_buttons();
}

void AlbumApp::render()
{
    const int w = g_canvas->width();
    const int h = g_canvas->height();

    g_canvas->fillScreen(TFT_BLACK);

    if (_img_buf) {
        g_canvas->drawJpg(_img_buf, _img_len, 0, 0, w, h, 0, 0);
    } else {
        g_canvas->setTextColor(TFT_WHITE);
        g_canvas->setFont(&fonts::Font4);
        g_canvas->drawString("Load Failed", (w-160)/2, h/2-14);
    }

    g_canvas->fillRect(0, h-28, w, 28, 0x2118);
    g_canvas->setTextColor(TFT_WHITE);
    g_canvas->setFont(&fonts::Font2);
    g_canvas->drawString("Scenery Album", 12, h-24);

    g_canvas->pushSprite(0, 0);
    M5.Display.display();
}

void AlbumApp::handle_buttons()
{
    if (M5.BtnA.wasClicked() || M5.BtnC.wasClicked()) _needs_refresh = true;
    if (M5.BtnB.wasClicked()) _needs_refresh = true;
    if (M5.BtnC.wasHold()) { ESP_LOGI(TAG, "Sleep"); pc_hal_deep_sleep(); }
}
