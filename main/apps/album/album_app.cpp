/**
 * PaperColor — Daily Photo Slideshow
 *
 * Stores 10 images in /sd/album/.  Updates daily via WiFi on first
 * use of each new day.  Falls back to existing images if no network.
 * TOP long press forces a manual update.
 */

#include "album_app.h"
#include "hal/hal.h"
#include "hal/battery.h"
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
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#include <esp_netif.h>
#include <esp_jpeg_dec.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <M5Unified.hpp>

static const char* TAG = "Album";

#define SLIDE_INTERVAL_MS  (30ULL * 60 * 1000)   // 30 min
#define CHECK_INTERVAL_MS  (60ULL * 60 * 1000)   // 1 hour — how often to check if day changed

// ── Config file (/sd/album/config.txt) ──────────────────────
// Key=value format.  Also reads legacy /sd/wifi.txt for backward compat.
// Keys: ssid, pass, dns, updated

static void led_no_network(void)
{
    led_async_flash(255, 120, 0, 4);  // orange flash ~2s
}

static void led_failure(void)
{
    led_async_flash(255, 0, 0, 4);    // red flash ~2s
}

static void led_success(void)
{
    led_async_flash(0, 255, 0, 4);    // green flash ~2s
}

static char s_dns_str[32] = "114.114.114.114";
#define CONFIG_PATH "/sd/album/config.txt"

// Read a value for key from a key=value file. Returns true if found.
static bool config_read_val(const char* path, const char* key, char* val, size_t val_sz)
{
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    size_t klen = strlen(key);
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            size_t vlen = strlen(line + klen + 1);
            if (vlen >= val_sz) vlen = val_sz - 1;
            memcpy(val, line + klen + 1, vlen);
            val[vlen] = '\0';
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

static void config_write_val(const char* path, const char* key, const char* val)
{
    // Read existing lines, update or append
    char tmp[512] = {};
    FILE* f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f))
            strncat(tmp, line, sizeof(tmp) - strlen(tmp) - 1);
        fclose(f);
    }
    // Remove old line with same key
    char* old = strstr(tmp, key);
    if (old) {
        char* nl = strchr(old, '\n');
        if (nl) memmove(old, nl + 1, strlen(nl + 1) + 1);
        else *old = '\0';
    }
    // Append new
    char newline[128];
    snprintf(newline, sizeof(newline), "%s=%s\n", key, val);
    strncat(tmp, newline, sizeof(tmp) - strlen(tmp) - 1);

    f = fopen(path, "w");
    if (f) { fputs(tmp, f); fclose(f); }
}

static bool load_wifi_from_sd(void)
{
    char ssid[64] = {}, pass[64] = {}, dns[32] = {};

    // Try new config.txt first, then legacy wifi.txt
    sd_card_lock(2000);
    bool ok = config_read_val(CONFIG_PATH, "ssid", ssid, sizeof(ssid)) &&
              config_read_val(CONFIG_PATH, "pass", pass, sizeof(pass));
    if (!ok) {
        // Legacy /sd/wifi.txt (line 1=SSID, line 2=pass, line 3=dns optional)
        FILE* f = fopen("/sd/wifi.txt", "r");
        if (f) {
            fgets(ssid, sizeof(ssid), f);
            fgets(pass, sizeof(pass), f);
            fgets(dns, sizeof(dns), f);
            fclose(f);
            ssid[strcspn(ssid, "\r\n")] = '\0';
            pass[strcspn(pass, "\r\n")] = '\0';
            ok = (strlen(ssid) > 0);
        }
    } else {
        config_read_val(CONFIG_PATH, "dns", dns, sizeof(dns));
    }
    sd_card_unlock();
    if (!ok) return false;

    if (strlen(dns) > 0) {
        dns[strcspn(dns, "\r\n")] = '\0';
        size_t n = strlen(dns);
        if (n >= sizeof(s_dns_str)) n = sizeof(s_dns_str) - 1;
        memcpy(s_dns_str, dns, n);
        s_dns_str[n] = '\0';
    }

    ESP_LOGI(TAG, "WiFi loaded: %s (DNS: %s)", ssid, s_dns_str);
    wifi_mgr_save_network(0, ssid, pass);
    return true;
}

static void save_wifi_to_sd(void)
{
    char ssid[WIFI_MAX_SSID_LEN + 1] = {}, pass[WIFI_MAX_PASS_LEN + 1] = {};
    if (!wifi_mgr_load_network(0, ssid, sizeof(ssid), pass, sizeof(pass))) return;
    if (strlen(ssid) == 0) return;

    sd_card_lock(2000);
    config_write_val(CONFIG_PATH, "ssid", ssid);
    config_write_val(CONFIG_PATH, "pass", pass);
    config_write_val(CONFIG_PATH, "dns", s_dns_str);
    sd_card_unlock();
    ESP_LOGI(TAG, "Config saved: %s (DNS: %s)", ssid, s_dns_str);
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
    // Already connected
    if (wifi_mgr_get_state() == WIFI_STATE_STA_OK) return true;

    ESP_LOGI(TAG, "Connecting WiFi (5s timeout)...");
    if (wifi_mgr_connect_sta(5000)) {
        set_dns();
        ESP_LOGI(TAG, "WiFi connected");
        return true;
    }

    ESP_LOGW(TAG, "WiFi not available");
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

static void draw_battery_icon(void)
{
    const int w = M5.Display.width();
    int pct = bat_get_pct();
    int segs = (pct + 9) / 20;
    if (segs > 5) segs = 5;

    // Smaller icon, top-right
    int bx = w - 36, by = 6;
    int bw = 30, bh = 13;

    g_canvas->drawRect(bx, by, bw, bh, TFT_BLACK);
    g_canvas->fillRect(bx + bw, by + 3, 3, bh - 6, TFT_BLACK);

    int seg_w = 4, seg_gap = 1, seg_h = 9;
    int sx = bx + 3, sy = by + 2;
    for (int i = 0; i < 5; i++) {
        int x = sx + i * (seg_w + seg_gap);
        if (i < segs)
            g_canvas->fillRect(x, sy, seg_w, seg_h, TFT_BLACK);
        else
            g_canvas->drawRect(x, sy, seg_w, seg_h, TFT_BLACK);
    }

    if (bat_is_charging()) {
        g_canvas->setTextColor(TFT_BLACK);
        g_canvas->setFont(&fonts::Font0);
        g_canvas->drawString("+", bx + 11, by);
    }
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

    // Overlay battery icon
    draw_battery_icon();

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
    sd_card_lock(2000);
    char buf[16] = {};
    config_read_val(CONFIG_PATH, "updated", buf, sizeof(buf));
    sd_card_unlock();
    int date = 0;
    if (buf[0]) date = atoi(buf);
    return date;
}

void AlbumApp::write_index_date(int date)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", date);
    sd_card_lock(2000);
    config_write_val(CONFIG_PATH, "updated", buf);
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

    if (!wifi_ensure_connected()) {
        led_no_network();
        return false;
    }

    led_async_breath_forever(0, 0, 255); // blue: downloading

    uint8_t* jpeg = nullptr;
    size_t len = 0;
    if (!http_fetch_one("https://bing.img.run/rand_1366x768.php", &jpeg, &len)) {
        led_failure();
        return false;
    }

    sd_card_lock(2000);
    unlink(path);  // remove old file if exists
    FILE* f = fopen(path, "w");
    if (f) { fwrite(jpeg, 1, len, f); fclose(f); }
    sd_card_unlock();

    free(jpeg);
    led_success();
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

bool AlbumApp::load_and_show(int index, bool fast)
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
    _decoded_buf = nullptr;
    _img_buf = jpeg;
    _img_len = (size_t)flen;

    ESP_LOGI(TAG, "[LOAD] image %d%s", index, fast ? " fast" : "");
    bat_update();
    return decode_and_render(_img_buf, _img_len, fast);
}

bool AlbumApp::decode_and_render(const uint8_t* jpeg, size_t len, bool fast)
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

    pc_hal_epd_refresh(fast);
    uint32_t t3 = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Image: decode %dms filter %dms epd %dms%s",
             (int)(t1 - t0), (int)(t2 - t1), (int)(t3 - t2), fast ? " fast" : "");
    return true;
}

// ── Navigation ──────────────────────────────────────────────

void AlbumApp::show_next(void)
{
    if (_total_images == 0 || _dl_in_progress) return;
    _current_idx = (_current_idx % _total_images) + 1;
    _last_slide_ms = esp_timer_get_time() / 1000;
    load_and_show(_current_idx, true);   // fast: button press
}

void AlbumApp::show_prev(void)
{
    if (_total_images == 0 || _dl_in_progress) return;
    _current_idx = (_current_idx > 1) ? _current_idx - 1 : _total_images;
    _last_slide_ms = esp_timer_get_time() / 1000;
    load_and_show(_current_idx, true);   // fast: button press
}

void AlbumApp::check_auto_advance(void)
{
    if (_total_images < 2 || _dl_in_progress) return;
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - _last_slide_ms >= SLIDE_INTERVAL_MS) {
        ESP_LOGI(TAG, "Auto-advance");
        show_next();
    }
}

void AlbumApp::check_daily_update(void)
{
    if (_dl_in_progress) return;
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
    if (!wifi_ensure_connected()) {
        ESP_LOGE(TAG, "No WiFi — can't fetch image");
        led_no_network();
        return false;
    }

    led_async_breath_forever(0, 0, 255);    // blue: downloading

    uint8_t* jpeg = nullptr;
    size_t len = 0;
    if (!http_fetch_one("https://bing.img.run/rand_1366x768.php", &jpeg, &len)) {
        ESP_LOGE(TAG, "HTTP fetch failed");
        led_failure();
        return false;
    }

    bat_update();                            // log battery when fetching

    led_success();                           // green 2s: ready to decode

    bool ok = decode_and_render(jpeg, len);
    free(jpeg);
    if (ok) _last_slide_ms = esp_timer_get_time() / 1000;
    return ok;
}

// ── Low-power sleep ─────────────────────────────────────────

static uint64_t s_last_activity_ms = 0;
#define IDLE_SLEEP_MS  (60ULL * 1000)  // 60s idle → sleep

void AlbumApp::go_to_sleep(void)
{
    // Check battery — if low, permanent shutdown
    if (bat_is_low()) {
        ESP_LOGW(TAG, "Battery low (%u%%), shutting down permanently", bat_get_pct());
        pc_hal_rtc_ram_write(0, (uint8_t)_current_idx);
        pc_hal_rtc_ram_write(1, (uint8_t)(_last_update_date & 0xFF));
        pc_hal_rtc_ram_write(2, (uint8_t)((_last_update_date >> 8) & 0xFF));
        if (_sd_mounted) { sd_card_unmount(); _sd_mounted = false; }
        pc_hal_deep_sleep();  // deep sleep, no timer wake
        return;
    }

    ESP_LOGI(TAG, "Deep sleep (%u%%), wake in 30 min or button", bat_get_pct());

    // Save state to RTC RAM
    pc_hal_rtc_ram_write(0, (uint8_t)_current_idx);
    pc_hal_rtc_ram_write(1, (uint8_t)(_last_update_date & 0xFF));
    pc_hal_rtc_ram_write(2, (uint8_t)((_last_update_date >> 8) & 0xFF));

    // Unmount SD
    if (_sd_mounted) { sd_card_unmount(); _sd_mounted = false; }

    // Deep sleep with RTC timer (30 min) + button wake (ext1 on G0/G1/G9/G10)
    M5.Display.sleep();
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup(30ULL * 60 * 1000000);  // 30 min
    esp_sleep_enable_ext1_wakeup((1ULL << 0) | (1ULL << 1) | (1ULL << 9) | (1ULL << 10),
                                  ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
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

    // Detect RTC wake → restore index and update date
    bool rtc_wake = pc_hal_is_rtc_wake();
    if (rtc_wake) {
        uint8_t idx = 0;
        if (pc_hal_rtc_ram_read(0, &idx) && idx >= 1 && idx <= ALBUM_MAX_IMAGES)
            _current_idx = idx;
        uint8_t lo = 0, hi = 0;
        pc_hal_rtc_ram_read(1, &lo);
        pc_hal_rtc_ram_read(2, &hi);
        _last_update_date = (hi << 8) | lo;
        ESP_LOGI(TAG, "RTC wake, restored idx=%d date=%d", _current_idx, _last_update_date);
    }

    _sd_mounted = sd_card_mount();

    if (!_sd_mounted) {
        ESP_LOGI(TAG, "No SD card — single-image mode");
        if (rtc_wake) {
            fetch_and_show_one();  // new image every 30min
            go_to_sleep();
        }
        // Button wake: EPD shows old image, don't refresh
        return true;
    }

    // ── SD mode ──
    ensure_album_folder();

    if (!rtc_wake) {
        // Button wake: EPD already shows the last image, don't refresh
        _last_update_date = read_index_date();
        _total_images = scan_folder_images();

        if (_total_images > 0) {
            _current_idx = 1;  // reset index, user can navigate with buttons
            _last_slide_ms = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "Wake %d images available", _total_images);
        }

        if (_total_images == 0) {
            refresh_all_images();
        } else {
            int today = get_today();
            if (today > _last_update_date) {
                ESP_LOGI(TAG, "New day (%d > %d), queuing download", today, _last_update_date);
                _dl_pending = true;
            }
        }
    } else {
        // RTC wake: advance to next image
        _total_images = scan_folder_images();
        if (_total_images > 0) {
            _current_idx = (_current_idx % _total_images) + 1;
            load_and_show(_current_idx);
            _last_slide_ms = esp_timer_get_time() / 1000;
        }

        // Check daily update
        int today = get_today();
        if (today > 0 && today > _last_update_date) {
            ESP_LOGI(TAG, "RTC wake new day (%d > %d), refreshing", today, _last_update_date);
            refresh_all_images();
        }

        // Go back to sleep immediately after RTC wake
        go_to_sleep();
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
            led_no_network();
        }
    }

    // All 10 downloaded — update index, but DON'T refresh display
    // Image 1 is already shown; slideshow timer will advance at 30min
    if (_total_images >= ALBUM_MAX_IMAGES) {
        int today = get_today();
        if (today > 0) write_index_date(today);
        _dl_pending = false;
        ESP_LOGI(TAG, "Download complete (10 images), slideshow continues");
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

    // ── Auto-sleep after idle timeout ──
    if (_dl_pending || _dl_in_progress) return;           // downloading
    if (spi_bus_get_owner() != SPI_OWNER_NONE) return;    // SPI busy

    uint64_t now = esp_timer_get_time() / 1000;
    if (now - s_last_activity_ms >= IDLE_SLEEP_MS) {
        go_to_sleep();
    }
}

// ── Buttons ─────────────────────────────────────────────────

void AlbumApp::handle_buttons()
{
    // Track any button activity for idle sleep timer
    if (BTN_UP.isPressed() || BTN_DOWN.isPressed() || BTN_TOP.isPressed())
        s_last_activity_ms = esp_timer_get_time() / 1000;

    // UP + DOWN held together → provisioning
    if (BTN_UP.isPressed() && BTN_DOWN.isPressed()) {
        if (!_btn_busy) {
            _btn_busy = true;
            // Try wifi.txt from SD first
            if (load_wifi_from_sd() && wifi_mgr_connect_sta(5000)) {
                set_dns();
                save_wifi_to_sd();
                ESP_LOGI(TAG, "Connected via SD wifi.txt");
            } else {
                ESP_LOGI(TAG, "UP+DOWN: AP provisioning");
                led_async_breath_forever(255, 200, 0);  // yellow: provisioning
                wifi_mgr_trigger_provisioning();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            _btn_busy = false;
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
