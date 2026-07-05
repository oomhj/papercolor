/**
 * PaperColor — Daily Photo Slideshow
 *
 * Stores 10 images in /sd/album/.  Updates daily via WiFi on first
 * use of each new day.  Falls back to existing images if no network.
 * TOP long press forces a manual update.
 */

#include "album_app.h"
#include "hal/hal.h"
#include "hal/sd_card.h"
#include "hal/led_driver.h"
#include "wifi_manager.h"
#include "filter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_jpeg_dec.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <nvs_flash.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <arpa/inet.h>
#include <M5Unified.hpp>

static const char* TAG = "Album";

#define SLIDE_INTERVAL_MS  (30ULL * 60 * 1000)   // 30 min
#define CHECK_INTERVAL_MS  (60ULL * 60 * 1000)   // 1 hour — how often to check if day changed

#ifndef ALBUM_SSID
#define ALBUM_SSID "Jason-home"
#endif
#ifndef ALBUM_PASS
#define ALBUM_PASS "admin1234"
#endif

// ── WiFi ────────────────────────────────────────────────────

static bool s_wifi_inited = false;

static bool wifi_ensure_connected(void)
{
    ESP_LOGI(TAG, "WiFi connecting...");

    if (!s_wifi_inited) {
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            e = nvs_flash_init();
        }
        if (e != ESP_OK) { ESP_LOGE(TAG, "NVS: %s", esp_err_to_name(e)); return false; }

        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&wcfg) != ESP_OK) { ESP_LOGE(TAG, "wifi init fail"); return false; }
        esp_wifi_set_mode(WIFI_MODE_STA);

        wifi_config_t wc = {};
        strcpy((char*)wc.sta.ssid, ALBUM_SSID);
        strcpy((char*)wc.sta.password, ALBUM_PASS);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_start();
        s_wifi_inited = true;
    }

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return true;
    }

    esp_wifi_connect();

    uint32_t deadline = esp_timer_get_time() / 1000 + 15000;
    while (esp_timer_get_time() / 1000 < deadline) {
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                for (int i = 0; i < 50; i++) {
                    esp_netif_ip_info_t ip;
                    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0)
                        break;
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_netif_dns_info_t dns{};
                dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("114.114.114.114");
                dns.ip.type = ESP_IPADDR_TYPE_V4;
                esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGW(TAG, "WiFi timeout (15s)");
    return false;
}

// ── HTTP fetch ──────────────────────────────────────────────

struct http_ctx { uint8_t* buf; size_t len; size_t cap; };

static esp_err_t http_event_handler(esp_http_client_event_t* evt)
{
    auto* ctx = (http_ctx*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t needed = ctx->len + evt->data_len + 4096;
        if (needed > ctx->cap) {
            uint8_t* nb = (uint8_t*)heap_caps_realloc(ctx->buf, needed,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!nb) nb = (uint8_t*)realloc(ctx->buf, needed);
            if (!nb) return ESP_FAIL;
            ctx->buf = nb;
            ctx->cap = needed;
        }
        memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
        ctx->len += evt->data_len;
    }
    return ESP_OK;
}

static bool http_fetch_one(const char* url, uint8_t** out, size_t* out_len)
{
    *out = nullptr; *out_len = 0;

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 20000;
    cfg.buffer_size = 8192;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.method = HTTP_METHOD_GET;
    cfg.max_redirection_count = 5;

    http_ctx acc = {};
    cfg.user_data = &acc;
    cfg.event_handler = http_event_handler;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;

    esp_http_client_set_header(c, "User-Agent",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
        "AppleWebKit/537.36 Chrome/149.0.0.0 Safari/537.36");

    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || status != 200) { free(acc.buf); return false; }

    // Find JPEG start
    size_t skip = 0;
    for (size_t i = 0; i + 1 < acc.len; i++)
        if (acc.buf[i] == 0xFF && acc.buf[i+1] == 0xD8) { skip = i; break; }
    if (skip > 0) { acc.len -= skip; memmove(acc.buf, acc.buf + skip, acc.len); }

    if (acc.len < 2 || acc.buf[0] != 0xFF || acc.buf[1] != 0xD8) { free(acc.buf); return false; }

    *out = acc.buf;
    *out_len = acc.len;
    return true;
}

// ── JPEG decode + render ────────────────────────────────────

static bool decode_jpeg(const uint8_t* jpeg, size_t len,
                         uint8_t** out, int* out_sw, int* out_sh,
                         int* out_crop_x, int* out_out_y)
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();

    jpeg_dec_config_t dcfg = DEFAULT_JPEG_DEC_CONFIG();
    dcfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;

    jpeg_dec_handle_t jdec = NULL;
    jpeg_dec_header_info_t hdr = {};
    jpeg_dec_io_t io = {};
    io.inbuf = (uint8_t*)jpeg;
    io.inbuf_len = (int)len;

    if (jpeg_dec_open(&dcfg, &jdec) != JPEG_ERR_OK) return false;
    bool ok_hdr = (jpeg_dec_parse_header(jdec, &io, &hdr) == JPEG_ERR_OK);
    jpeg_dec_close(jdec);
    if (!ok_hdr) return false;

    int sh = 400;
    int sw = (hdr.width * sh + hdr.height / 2) / hdr.height;
    sw = ((sw + 4) / 8) * 8;
    if (sw < w) sw = w;
    int crop_x = (sw > w) ? (sw - w) / 2 : 0;
    int out_y  = (h - sh) / 2;

    dcfg.scale.width = sw;
    dcfg.scale.height = sh;

    if (jpeg_dec_open(&dcfg, &jdec) != JPEG_ERR_OK) return false;
    jpeg_dec_parse_header(jdec, &io, &hdr);
    int out_len = 0;
    jpeg_dec_get_outbuf_len(jdec, &out_len);

    bool ok = false;
    if (out_len > 0) {
        uint8_t* buf = (uint8_t*)jpeg_calloc_align(out_len, 16);
        if (buf) {
            io.outbuf = buf; io.out_size = out_len;
            if (jpeg_dec_process(jdec, &io) == JPEG_ERR_OK) {
                *out = buf; *out_sw = sw; *out_sh = sh;
                *out_crop_x = crop_x; *out_out_y = out_y;
                ok = true;
            } else { jpeg_free_align(buf); }
        }
    }
    jpeg_dec_close(jdec);
    return ok;
}

static void filter_and_display(uint8_t* decoded, int sw, int sh,
                                int crop_x, int out_y)
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    g_canvas->fillScreen(TFT_WHITE);

    const uint16_t* src = (const uint16_t*)decoded + out_y * sw + crop_x;
    uint8_t* dither = (uint8_t*)malloc(w * h);
    if (!dither) return;

    uint16_t* crop = (uint16_t*)malloc(w * h * 2);
    if (!crop) { free(dither); return; }

    for (int y = 0; y < h; y++)
        memcpy(crop + y * w, src + y * sw, w * 2);

    FILTERS[1].fn(crop, dither, w, h);  // Floyd-Steinberg
    g_canvas->pushImage(0, 0, w, h, dither);
    free(crop);
    free(dither);
}

// ═══════════════════════════════════════════════════════════════
//  AlbumApp implementation
// ═══════════════════════════════════════════════════════════════

// ── Folder / Date helpers ───────────────────────────────────

int AlbumApp::get_today(void)
{
    m5::rtc_date_t d;
    m5::rtc_time_t t;
    if (!M5.Rtc.getDateTime(&d, &t)) return 0;
    if (d.year < 2024 || d.year > 2099) return 0;
    return d.year * 10000 + d.month * 100 + d.date;
}

int AlbumApp::read_index_date(void)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/index.txt", ALBUM_DIR);

    sd_card_lock(UINT32_MAX);
    FILE* f = fopen(path, "r");
    if (!f) { sd_card_unlock(); return 0; }
    int date = 0;
    fscanf(f, "%d", &date);
    fclose(f);
    sd_card_unlock();
    return date;
}

void AlbumApp::write_index_date(int date)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/index.txt", ALBUM_DIR);

    sd_card_lock(UINT32_MAX);
    FILE* f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", date); fclose(f); }
    sd_card_unlock();
}

bool AlbumApp::ensure_album_folder(void)
{
    DIR* dir = opendir(ALBUM_DIR);
    if (!dir) {
        ESP_LOGI(TAG, "Creating %s", ALBUM_DIR);
        if (mkdir(ALBUM_DIR, 0777) != 0 && errno != EEXIST) {
            ESP_LOGE(TAG, "mkdir failed: errno=%d", errno);
            return false;
        }
    } else {
        closedir(dir);
    }
    return true;
}

int AlbumApp::scan_folder_images(void)
{
    DIR* dir = opendir(ALBUM_DIR);
    if (!dir) return 0;

    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            int n = 0;
            if (sscanf(entry->d_name, "%d.jpg", &n) == 1 && n >= 1 && n <= ALBUM_MAX_IMAGES)
                count++;
        }
    }
    closedir(dir);
    return count;
}

// ── Download ────────────────────────────────────────────────

bool AlbumApp::download_one(int index)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/%d.jpg", ALBUM_DIR, index);
    ESP_LOGI(TAG, "DL %d/%d → %s", index, ALBUM_MAX_IMAGES, path);

    led_async_breath_forever(0, 0, 255);

    uint8_t* jpeg = nullptr;
    size_t len = 0;
    if (!http_fetch_one("https://bing.img.run/rand_1366x768.php", &jpeg, &len))
        return false;

    sd_card_lock(UINT32_MAX);
    FILE* f = fopen(path, "w");
    if (f) { fwrite(jpeg, 1, len, f); fclose(f); }
    sd_card_unlock();

    free(jpeg);
    led_async_flash(0, 255, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    return true;
}

bool AlbumApp::update_images(void)
{
    ESP_LOGI(TAG, "Updating images from network...");
    if (!wifi_ensure_connected()) {
        ESP_LOGW(TAG, "No network — keeping existing images");
        return false;
    }

    // Delete old images
    for (int i = 1; i <= ALBUM_MAX_IMAGES; i++) {
        char p[64]; snprintf(p, sizeof(p), "%s/%d.jpg", ALBUM_DIR, i);
        unlink(p);
    }

    int ok = 0;
    for (int i = 1; i <= ALBUM_MAX_IMAGES; i++) {
        if (download_one(i)) ok++; else break;
    }

    if (ok == 0) {
        ESP_LOGE(TAG, "All downloads failed");
        return false;
    }

    int today = get_today();
    if (today > 0) write_index_date(today);
    ESP_LOGI(TAG, "Updated: %d images, date=%d", ok, today);
    return true;
}

// ── Load & display ──────────────────────────────────────────

bool AlbumApp::load_and_show(int index)
{
    if (index < 1) return false;

    char path[64];
    snprintf(path, sizeof(path), "%s/%d.jpg", ALBUM_DIR, index);

    sd_card_lock(UINT32_MAX);
    FILE* f = fopen(path, "r");
    if (!f) { sd_card_unlock(); return false; }

    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen <= 0 || flen > 2 * 1024 * 1024) { fclose(f); sd_card_unlock(); return false; }

    uint8_t* jpeg = (uint8_t*)malloc((size_t)flen);
    if (!jpeg) { fclose(f); sd_card_unlock(); return false; }

    size_t got = fread(jpeg, 1, (size_t)flen, f);
    fclose(f);
    sd_card_unlock();

    if (got != (size_t)flen || got < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
        free(jpeg); return false;
    }

    if (_img_buf) { free(_img_buf); _img_buf = nullptr; }
    // _decoded_buf is freed + set to null inside decode_and_render()
    _decoded_buf = nullptr;
    _img_buf = jpeg;
    _img_len = (size_t)flen;

    ESP_LOGI(TAG, "[LOAD] image %d", index);
    return decode_and_render(_img_buf, _img_len);
}

bool AlbumApp::decode_and_render(const uint8_t* jpeg, size_t len)
{
    if (_decoded_buf) { free(_decoded_buf); _decoded_buf = nullptr; }

    uint32_t t0 = esp_timer_get_time() / 1000;

    bool ok = decode_jpeg(jpeg, len, &_decoded_buf, &_decoded_sw, &_decoded_sh,
                           &_decoded_crop_x, &_decoded_out_y);
    if (!ok) return false;

    uint32_t t1 = esp_timer_get_time() / 1000;
    filter_and_display(_decoded_buf, _decoded_sw, _decoded_sh,
                        _decoded_crop_x, _decoded_out_y);
    uint32_t t2 = esp_timer_get_time() / 1000;

    if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }

    pc_hal_epd_refresh();
    uint32_t t3 = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Image: decode %dms filter %dms epd %dms",
             (int)(t1 - t0), (int)(t2 - t1), (int)(t3 - t2));
    return true;
}

// ── Navigation ──────────────────────────────────────────────

void AlbumApp::show_next(void)
{
    if (_total_images == 0) return;
    _current_idx = (_current_idx % _total_images) + 1;
    _last_slide_ms = esp_timer_get_time() / 1000;
    load_and_show(_current_idx);
}

void AlbumApp::show_prev(void)
{
    if (_total_images == 0) return;
    _current_idx = (_current_idx > 1) ? _current_idx - 1 : _total_images;
    _last_slide_ms = esp_timer_get_time() / 1000;
    load_and_show(_current_idx);
}

void AlbumApp::check_auto_advance(void)
{
    if (_total_images < 2) return;
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - _last_slide_ms >= SLIDE_INTERVAL_MS) {
        ESP_LOGI(TAG, "Auto-advance");
        show_next();
    }
}

void AlbumApp::check_daily_update(void)
{
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - _last_date_check_ms < CHECK_INTERVAL_MS) return;
    _last_date_check_ms = now_ms;

    int today = get_today();
    if (today == 0) return;  // no RTC

    if (today > _last_update_date) {
        ESP_LOGI(TAG, "New day (%d > %d), updating images...", today, _last_update_date);
        if (update_images()) {
            _total_images = scan_folder_images();
            _last_update_date = today;
            if (_total_images > 0) {
                _current_idx = 1;
                load_and_show(_current_idx);
                _last_slide_ms = esp_timer_get_time() / 1000;
            }
        } else {
            ESP_LOGW(TAG, "Update failed, using existing images");
        }
    }
}

// ── No-SD mode: fetch 1 image, display immediately ─────────

bool AlbumApp::fetch_and_show_one(void)
{
    led_async_breath_forever(255, 165, 0);  // orange: WiFi

    if (!wifi_ensure_connected()) {
        ESP_LOGE(TAG, "No WiFi — can't fetch image");
        led_async_flash(255, 0, 0, 3);      // red
        return false;
    }

    led_async_breath_forever(0, 0, 255);    // blue: HTTP

    uint8_t* jpeg = nullptr;
    size_t len = 0;
    if (!http_fetch_one("https://bing.img.run/rand_1366x768.php", &jpeg, &len)) {
        ESP_LOGE(TAG, "HTTP fetch failed");
        led_async_flash(255, 0, 0, 3);
        return false;
    }

    led_async_flash(0, 255, 0, 3);          // green: decode

    bool ok = decode_and_render(jpeg, len);
    free(jpeg);
    if (ok) _last_slide_ms = esp_timer_get_time() / 1000;
    return ok;
}

// ── Lifecycle ───────────────────────────────────────────────

bool AlbumApp::init()
{
    _current_idx = 1;
    _total_images = 0;
    _last_slide_ms = 0;
    _last_date_check_ms = 0;
    _filter_idx = 1;
    _img_buf = nullptr;
    _decoded_buf = nullptr;

    wifi_mgr_init();
    led_init(); led_async_start();

    _sd_mounted = sd_card_mount();

    if (!_sd_mounted) {
        ESP_LOGI(TAG, "No SD card — single-image mode");
        fetch_and_show_one();
        return true;
    }

    // ── SD mode: 10-image slideshow ──
    ensure_album_folder();

    _last_update_date = read_index_date();
    _total_images = scan_folder_images();
    ESP_LOGI(TAG, "Last update: %d, images: %d", _last_update_date, _total_images);

    // Download if no images at all
    if (_total_images == 0) {
        ESP_LOGI(TAG, "No images, downloading 10...");
        if (update_images()) {
            _total_images = scan_folder_images();
            _last_update_date = get_today();
        }
    }

    if (_total_images > 0) {
        _current_idx = 1;
        load_and_show(_current_idx);
        _last_slide_ms = esp_timer_get_time() / 1000;
    } else {
        g_canvas->fillScreen(TFT_WHITE);
        g_canvas->setTextColor(TFT_RED);
        g_canvas->setFont(&fonts::Font4);
        g_canvas->drawString("No Images", (400 - 140) / 2, 600 / 2 - 14);
        pc_hal_epd_refresh();
    }

    _last_date_check_ms = esp_timer_get_time() / 1000;
    return true;
}

void AlbumApp::deinit()
{
    _running = false;
    if (_img_buf) { free(_img_buf); _img_buf = nullptr; }
    if (_decoded_buf) { free(_decoded_buf); _decoded_buf = nullptr; }
    if (_sd_mounted) { sd_card_unmount(); _sd_mounted = false; }
}

void AlbumApp::start() { _running = true; }
void AlbumApp::stop()  { _running = false; }
void AlbumApp::refresh() { _needs_refresh = true; }

void AlbumApp::update()
{
    if (!_running) return;

    if (_sd_mounted) {
        check_auto_advance();
        check_daily_update();
    } else {
        // No-SD: every 30 min, fetch a new image
        uint64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - _last_slide_ms >= SLIDE_INTERVAL_MS) {
            _last_slide_ms = now_ms;
            fetch_and_show_one();
        }
    }
    handle_buttons();
}

// ── Buttons ─────────────────────────────────────────────────

void AlbumApp::handle_buttons()
{
    if (_sd_mounted) {
        // SD mode: slideshow with prev/next
        if (BTN_UP.wasClicked()) { show_prev(); }
        if (BTN_DOWN.wasClicked()) { show_next(); }
        if (BTN_TOP.wasHold()) {
            ESP_LOGI(TAG, "TOP hold: re-download 10 images");
            if (update_images()) {
                _total_images = scan_folder_images();
                _last_update_date = get_today();
                if (_total_images > 0) {
                    _current_idx = 1;
                    load_and_show(_current_idx);
                    _last_slide_ms = esp_timer_get_time() / 1000;
                }
            }
        }
    } else {
        // No-SD mode: single image, TOP refreshes
        if (BTN_TOP.wasHold()) {
            ESP_LOGI(TAG, "TOP hold: refresh 1 image");
            fetch_and_show_one();
        }
    }
}
