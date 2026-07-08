/**
 * PaperColor — SlideShow Controller Implementation
 */

#include "slide_show.h"
#include "hal/sd_card.h"
#include "hal/battery.h"
#include "hal/led_driver.h"
#include "hal/hal.h"
#include "wifi_manager.h"
#include "image_downloader.h"
#include "image_renderer.h"
#include "config_file.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <M5Unified.hpp>
#include <esp_jpeg_dec.h>

static const char* TAG = "Slide";
#define ALBUM_DIR   "/sd/album"
#define CONFIG_PATH "/sd/album/config.txt"

// ═══════════════════════════════════════════════════════════════
//  Date helpers
// ═══════════════════════════════════════════════════════════════

int SlideShow::get_today(void)
{
    m5::rtc_date_t d;
    m5::rtc_time_t t;
    if (!M5.Rtc.getDateTime(&d, &t)) return 0;
    if (d.year < 2024 || d.year > 2099) return 0;
    return d.year * 10000 + d.month * 100 + d.date;
}

int SlideShow::read_index_date(void)
{
    sd_card_lock(2000);
    char buf[16] = {};
    config_read_val(CONFIG_PATH, "updated", buf, sizeof(buf));
    sd_card_unlock();
    int date = 0;
    if (buf[0]) date = atoi(buf);
    return date;
}

void SlideShow::write_index_date(int date)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", date);
    sd_card_lock(2000);
    config_write_val(CONFIG_PATH, "updated", buf);
    sd_card_unlock();
}

// ═══════════════════════════════════════════════════════════════
//  SD folder
// ═══════════════════════════════════════════════════════════════

bool SlideShow::ensure_album_folder(void)
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

int SlideShow::scan_folder_images(void)
{
    DIR* dir = opendir(ALBUM_DIR);
    if (!dir) return 0;
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            int n = 0;
            if (sscanf(entry->d_name, "%d.jpg", &n) == 1 && n >= 1 && n <= SS_MAX_IMAGES)
                count++;
        }
    }
    closedir(dir);
    return count;
}

// ═══════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════

void SlideShow::deinit()
{
    if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
    if (_decoded_buf) { jpeg_free_align(_decoded_buf); _decoded_buf = nullptr; }
}

void SlideShow::init(int today)
{
    current_idx = 1;
    total_images = 0;
    _last_slide_ms = 0;
    _last_date_check_ms = 0;
    dl_pending = false;
    dl_in_progress = false;
    _dl_fail_count = 0;
    _dl_last_fail_ms = 0;
    _img_buf = nullptr;
    _decoded_buf = nullptr;

    if (today > 0) {
        int cfg_date = read_index_date();
        if (cfg_date > 20240000) last_update_date = cfg_date;
    }
}

// ═══════════════════════════════════════════════════════════════
//  Download
// ═══════════════════════════════════════════════════════════════

bool SlideShow::download_one(int index)
{
    ESP_LOGI(TAG, "DL %d/%d", index, SS_MAX_IMAGES);
    led_async_breath_forever(0, 0, 255);

    uint8_t* jpeg = nullptr;
    size_t len = 0;
    if (!dl_fetch_default(&jpeg, &len)) {
        led_async_flash(255, 0, 0, 4);
        return false;
    }

    sd_card_lock(2000);
    bool saved = dl_save(ALBUM_DIR, index, jpeg, len);
    sd_card_unlock();
    free(jpeg);

    if (!saved) { led_async_flash(255, 0, 0, 4); return false; }
    led_async_flash(0, 255, 0, 4);
    vTaskDelay(pdMS_TO_TICKS(300));
    return true;
}

void SlideShow::refresh_all_images(void)
{
    ESP_LOGI(TAG, "Refresh: dl 1 → show → queue rest");
    _dl_fail_count = 0;  // reset backoff on manual refresh
    total_images = 0;

    if (download_one(1)) {
        total_images = 1;
        current_idx = 1;
        load_and_show(current_idx);
        _last_slide_ms = esp_timer_get_time() / 1000;
        write_index_date(get_today());
        dl_pending = true;
    } else {
        total_images = scan_folder_images();
        if (total_images > 0)
            ESP_LOGI(TAG, "Refresh failed, restored %d cached images", total_images);
    }
}

void SlideShow::run_pending_download(void)
{
    if (!dl_pending || dl_in_progress) return;
    dl_in_progress = true;

    if (total_images < SS_MAX_IMAGES) {
        // ── Check backoff ──────────────────────────────────
        if (_dl_fail_count > 0) {
            uint64_t backoff_ms = 5ULL * 60 * 1000;  // 5 min base
            // Exponential backoff: 5min, 10min, 20min, 40min, 60min (cap)
            uint64_t capped = backoff_ms * (1ULL << (_dl_fail_count - 1));
            if (capped > 60ULL * 60 * 1000) capped = 60ULL * 60 * 1000;
            uint64_t now_ms = esp_timer_get_time() / 1000;
            if (now_ms < _dl_last_fail_ms + capped) {
                ESP_LOGW(TAG, "Backoff: %lu/%lu s until retry", 
                    (now_ms - _dl_last_fail_ms) / 1000,
                    capped / 1000);
                dl_in_progress = false;
                return;
            }
        }

        ESP_LOGI(TAG, "Downloading %d..%d ...", total_images + 1, SS_MAX_IMAGES);
        led_async_breath_forever(0, 0, 255);

        int start = total_images + 1;
        bool any_failed = false;
        for (int i = start; i <= SS_MAX_IMAGES; i++) {
            if (download_one(i)) {
                total_images = i;
                write_index_date(get_today());
                _dl_fail_count = 0;  // reset on success
            } else {
                any_failed = true;
                break;
            }
        }

        if (any_failed) {
            _dl_fail_count++;
            _dl_last_fail_ms = esp_timer_get_time() / 1000;
            ESP_LOGW(TAG, "DL fail #%d, backoff %lu min", 
                _dl_fail_count,
                (5ULL * (1ULL << (_dl_fail_count - 1))) / 60);
        }
    }

    if (total_images >= SS_MAX_IMAGES) {
        _dl_fail_count = 0;
        int today = get_today();
        if (today > 0) write_index_date(today);
        dl_pending = false;
        ESP_LOGI(TAG, "Download complete (%d images), sleep in 5s", SS_MAX_IMAGES);
        dl_in_progress = false;
        vTaskDelay(pdMS_TO_TICKS(5000));
        dl_pending = false;  // signal caller to sleep
        return;
    }

    // ── Max backoff reached — give up and sleep ─────────────
    // Fail count 5 means backoff already hit 60min cap
    if (_dl_fail_count >= 5 && total_images > 0) {
        ESP_LOGW(TAG, "Gave up after %d failures, sleeping with %d cached images",
                 _dl_fail_count, total_images);
        _dl_fail_count = 0;
        dl_pending = false;
    }
    dl_in_progress = false;
}

// ═══════════════════════════════════════════════════════════════
//  Decode + render pipeline
// ═══════════════════════════════════════════════════════════════

bool SlideShow::decode_and_render(const uint8_t* jpeg, size_t len, bool fast)
{
    if (_decoded_buf) { jpeg_free_align(_decoded_buf); _decoded_buf = nullptr; }

    bool ok = ren_decode_jpeg(jpeg, len, &_decoded_buf, &_decoded_sw, &_decoded_sh,
                               &_decoded_crop_x, &_decoded_out_y);
    if (!ok) return false;

    ok = ren_render(_decoded_buf, _decoded_sw, _decoded_sh,
                    _decoded_crop_x, _decoded_out_y, fast);

    if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
    return ok;
}

// ═══════════════════════════════════════════════════════════════
//  Load & display
// ═══════════════════════════════════════════════════════════════

bool SlideShow::load_and_show(int index, bool fast)
{
    if (index < 1) return false;
    led_async_breath_forever(0, 0, 255);

    char path[64];
    snprintf(path, sizeof(path), "%s/%d.jpg", ALBUM_DIR, index);

    sd_card_lock(2000);
    FILE* f = fopen(path, "r");
    if (!f) { sd_card_unlock(); led_async_flash(255, 0, 0, 4); return false; }

    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen <= 0 || flen > 2 * 1024 * 1024) { fclose(f); sd_card_unlock(); led_async_flash(255, 0, 0, 4); return false; }

    uint8_t* jpeg = (uint8_t*)malloc((size_t)flen);
    if (!jpeg) { fclose(f); sd_card_unlock(); led_async_flash(255, 0, 0, 4); return false; }

    size_t got = fread(jpeg, 1, (size_t)flen, f);
    fclose(f);
    sd_card_unlock();

    if (got != (size_t)flen || got < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
        free(jpeg); led_async_flash(255, 0, 0, 4); return false;
    }

    if (_img_buf) { free(_img_buf); _img_buf = nullptr; }
    _decoded_buf = nullptr;
    _img_buf = jpeg;
    _img_len = (size_t)flen;

    ESP_LOGI(TAG, "[LOAD] image %d%s", index, fast ? " fast" : "");
    bat_update();

    bool ok = decode_and_render(_img_buf, _img_len, fast);
    if (!ok) led_async_flash(255, 0, 0, 4);
    return ok;
}

bool SlideShow::fetch_and_show_one(void)
{
    led_async_breath_forever(0, 0, 255);

    uint8_t* jpeg = nullptr;
    size_t len = 0;
    if (!dl_fetch_default(&jpeg, &len)) {
        led_async_flash(255, 0, 0, 4);
        return false;
    }

    bat_update();
    led_async_flash(0, 255, 0, 4);

    bool ok = decode_and_render(jpeg, len);
    free(jpeg);
    if (ok) _last_slide_ms = esp_timer_get_time() / 1000;
    return ok;
}

// ═══════════════════════════════════════════════════════════════
//  Navigation
// ═══════════════════════════════════════════════════════════════

void SlideShow::show_next(void)
{
    if (total_images == 0 || dl_in_progress) return;
    current_idx = (current_idx % total_images) + 1;
    _last_slide_ms = esp_timer_get_time() / 1000;
    load_and_show(current_idx);
}

void SlideShow::show_prev(void)
{
    if (total_images == 0 || dl_in_progress) return;
    current_idx = (current_idx > 1) ? current_idx - 1 : total_images;
    _last_slide_ms = esp_timer_get_time() / 1000;
    load_and_show(current_idx);
}

// ═══════════════════════════════════════════════════════════════
//  Auto-advance / daily update
// ═══════════════════════════════════════════════════════════════

void SlideShow::check_auto_advance(void)
{
    if (total_images < 2 || dl_in_progress) return;
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - _last_slide_ms >= (30ULL * 60 * 1000)) {
        ESP_LOGI(TAG, "Auto-advance");
        show_next();
    }
}

void SlideShow::check_daily_update(int today)
{
    if (dl_in_progress) return;
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - _last_date_check_ms < (60ULL * 60 * 1000)) return;
    _last_date_check_ms = now_ms;

    if (today == 0) return;
    if (today > last_update_date) {
        ESP_LOGI(TAG, "New day (%d > %d), refreshing", today, last_update_date);
        last_update_date = today;
        refresh_all_images();
    }
}
