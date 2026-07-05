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
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_jpeg_dec.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <M5Unified.hpp>

static const char* TAG = "Album";

#define SLIDE_INTERVAL_MS  (30ULL * 60 * 1000)   // 30 min
#define CHECK_INTERVAL_MS  (60ULL * 60 * 1000)   // 1 hour — how often to check if day changed

// ── WiFi config from SD (/sd/wifi.txt) ─────────────────────
// Format: first line = SSID, second line = password

static char s_dns_str[32] = "114.114.114.114";  // default DNS, overridable via wifi.txt

static bool load_wifi_from_sd(void)
{
    char ssid[64] = {}, pass[64] = {}, dns[32] = {};

    sd_card_lock(2000);
    FILE* f = fopen("/sd/wifi.txt", "r");
    if (!f) { sd_card_unlock(); return false; }

    if (!fgets(ssid, sizeof(ssid), f) || !fgets(pass, sizeof(pass), f)) {
        fclose(f); sd_card_unlock(); return false;
    }
    // Optional 3rd line: DNS server
    fgets(dns, sizeof(dns), f);
    fclose(f); sd_card_unlock();

    ssid[strcspn(ssid, "\r\n")] = '\0';
    pass[strcspn(pass, "\r\n")] = '\0';
    if (strlen(ssid) == 0) return false;

    if (strlen(dns) > 0) {
        dns[strcspn(dns, "\r\n")] = '\0';
        if (strlen(dns) > 0) strncpy(s_dns_str, dns, sizeof(s_dns_str) - 1);
    }

    ESP_LOGI(TAG, "WiFi loaded from SD: %s (DNS: %s)", ssid, s_dns_str);
    wifi_mgr_save_network(0, ssid, pass);
    return true;
}

static void save_wifi_to_sd(void)
{
    char ssid[WIFI_MAX_SSID_LEN + 1] = {}, pass[WIFI_MAX_PASS_LEN + 1] = {};
    if (!wifi_mgr_load_network(0, ssid, sizeof(ssid), pass, sizeof(pass))) return;
    if (strlen(ssid) == 0) return;

    sd_card_lock(2000);
    FILE* f = fopen("/sd/wifi.txt", "w");
    if (f) { fprintf(f, "%s\n%s\n%s\n", ssid, pass, s_dns_str); fclose(f); }
    sd_card_unlock();
    ESP_LOGI(TAG, "WiFi saved to /sd/wifi.txt: %s (DNS: %s)", ssid, s_dns_str);
}

static void set_dns(void)
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return;
    esp_netif_dns_info_t dns{};
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(s_dns_str);
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    ESP_LOGI(TAG, "DNS set to %s", s_dns_str);
}

static bool wifi_ensure_connected(void)
{
    if (wifi_mgr_get_state() == WIFI_STATE_STA_OK) return true;

    // 1. Try saved networks (NVS, from provisioning or previous SD load)
    if (wifi_mgr_connect_sta(15000)) {
        set_dns();
        save_wifi_to_sd();   // persist to SD
        return true;
    }

    // 2. Try SD card wifi.txt
    if (load_wifi_from_sd() && wifi_mgr_connect_sta(15000)) {
        set_dns();
        save_wifi_to_sd();
        return true;
    }

    // 3. All failed — start AP provisioning
    ESP_LOGW(TAG, "No WiFi config, starting AP provisioning...");
    wifi_mgr_trigger_provisioning();
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

    sd_card_lock(2000);
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

    sd_card_lock(2000);
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
    if (!http_fetch_one("https://bing.img.run/rand_1366x768.php", &jpeg, &len)) {
        led_async_flash(255, 0, 0, 3);  // red: HTTP failed
        return false;
    }

    sd_card_lock(2000);
    unlink(path);  // remove old file if exists
    FILE* f = fopen(path, "w");
    if (f) { fwrite(jpeg, 1, len, f); fclose(f); }
    sd_card_unlock();

    free(jpeg);
    led_async_flash(0, 255, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    return true;
}

void AlbumApp::refresh_all_images(void)
{
    ESP_LOGI(TAG, "Refresh: dl 1 → show → queue rest");

    _total_images = 0;

    // Download 1st, show immediately
    if (download_one(1)) {
        _total_images = 1;
        _current_idx = 1;
        load_and_show(_current_idx);
        _last_slide_ms = esp_timer_get_time() / 1000;
        _dl_pending = true;   // 2..10 in background
    }
}

// ── Load & display ──────────────────────────────────────────

bool AlbumApp::load_and_show(int index)
{
    if (index < 1) return false;

    char path[64];
    snprintf(path, sizeof(path), "%s/%d.jpg", ALBUM_DIR, index);

    sd_card_lock(2000);
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
        ESP_LOGI(TAG, "New day (%d > %d), refreshing", today, _last_update_date);
        _last_update_date = today;
        refresh_all_images();
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
    _dl_pending = false;
    _dl_in_progress = false;
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

    // 1. Show cached image immediately (user sees picture right away)
    if (_total_images > 0) {
        _current_idx = 1;
        load_and_show(_current_idx);
        _last_slide_ms = esp_timer_get_time() / 1000;
    }

    // 2. No images → use unified refresh (dl 1 → show → queue rest)
    if (_total_images == 0) {
        refresh_all_images();
    } else {
        int today = get_today();
        if (today > _last_update_date) {
            ESP_LOGI(TAG, "New day (%d > %d), queuing download", today, _last_update_date);
            _dl_pending = true;  // download in update() — no blocking
        } else {
            ESP_LOGI(TAG, "Images up to date (%d)", _last_update_date);
        }
    }

    // Show "No Images" only if truly nothing
    if (_total_images == 0) {
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
void AlbumApp::refresh() { _dl_pending = true; }

void AlbumApp::run_pending_download(void)
{
    if (!_dl_pending || _dl_in_progress) return;
    _dl_in_progress = true;

    // Only download missing images (don't delete existing)
    if (_total_images < ALBUM_MAX_IMAGES) {
        ESP_LOGI(TAG, "Downloading %d..%d ...", _total_images + 1, ALBUM_MAX_IMAGES);
        led_async_breath_forever(0, 0, 255);

        if (wifi_ensure_connected()) {
            int start = _total_images + 1;
            for (int i = start; i <= ALBUM_MAX_IMAGES; i++) {
                if (download_one(i)) _total_images = i;
                else break;
            }
        } else {
            ESP_LOGW(TAG, "WiFi failed — deferring download");
            led_async_flash(255, 0, 0, 3);  // red: no network
        }
    }

    // If we got all 10, mark done, update index, show first new image
    if (_total_images >= ALBUM_MAX_IMAGES) {
        int today = get_today();
        if (today > 0) write_index_date(today);
        _dl_pending = false;
        _current_idx = 1;
        load_and_show(_current_idx);
        _last_slide_ms = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "Download complete, showing image 1");
    }

    _dl_in_progress = false;
}

void AlbumApp::update()
{
    if (!_running) return;

    // Run deferred download first (if queued in init)
    run_pending_download();

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
    // UP + DOWN held together → provisioning
    if (BTN_UP.isPressed() && BTN_DOWN.isPressed()) {
        if (!_dl_in_progress) {  // debounce
            _dl_in_progress = true;
            ESP_LOGI(TAG, "UP+DOWN: provisioning");
            wifi_mgr_trigger_provisioning();
            vTaskDelay(pdMS_TO_TICKS(500));  // prevent re-trigger
            _dl_in_progress = false;
        }
        return;
    }

    if (_sd_mounted) {
        if (BTN_UP.wasClicked()) { show_prev(); }
        if (BTN_DOWN.wasClicked()) { show_next(); }
        if (BTN_TOP.wasHold()) {
            ESP_LOGI(TAG, "TOP hold: refresh");
            refresh_all_images();
        }
    } else {
        if (BTN_TOP.wasHold()) {
            ESP_LOGI(TAG, "TOP hold: refresh 1 image");
            fetch_and_show_one();
        }
    }
}
