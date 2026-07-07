/**
 * PaperColor — Daily Photo Slideshow
 *
 * Lifecycle orchestrator.  Delegates to SlideShow, PowerManager,
 * ImageDownloader, ImageRenderer, wifi_manager.
 */

#include "album_app.h"
#include "hal/hal.h"
#include "hal/battery.h"
#include "hal/sd_card.h"
#include "hal/led_driver.h"
#include "hal/spi_bus.h"
#include "wifi_manager.h"
#include "wifi_provisioning.h"
#include "config_file.h"
#include "image_downloader.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <M5Unified.hpp>
#include <esp_netif.h>

static const char* TAG = "Album";

// ── Forward declarations for static helpers ──────────────────

static void led_failure(void)    { led_async_flash(255, 0, 0, 4); }
static void led_success(void)    { led_async_flash(0, 255, 0, 4); }
#define CONFIG_FILE "/sd/album/config.txt"

// ── WiFi helpers (static, shared with provisioning) ──────────

static char s_dns_str[32] = "114.114.114.114";

static void set_dns(void)
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) return;
    esp_netif_dns_info_t dns{};
    dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(s_dns_str);
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
}

static bool wifi_ensure_connected(void)
{
    if (wifi_mgr_get_state() == WIFI_STATE_STA_OK) return true;
    ESP_LOGI(TAG, "Connecting WiFi (5s timeout)...");
    if (wifi_mgr_connect_sta(5000)) {
        set_dns();
        return true;
    }
    return false;
}

static bool load_wifi_from_sd(void)
{
    char ssid[64] = {}, pass[64] = {}, dns[32] = {};
    char auth[16] = {}, identity[64] = {}, username[64] = {};

    sd_card_lock(2000);
    bool ok = config_read_val(CONFIG_FILE, "ssid", ssid, sizeof(ssid)) &&
              config_read_val(CONFIG_FILE, "pass", pass, sizeof(pass));
    if (!ok) {
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
        config_read_val(CONFIG_FILE, "dns", dns, sizeof(dns));
        config_read_val(CONFIG_FILE, "auth", auth, sizeof(auth));
        config_read_val(CONFIG_FILE, "identity", identity, sizeof(identity));
        config_read_val(CONFIG_FILE, "username", username, sizeof(username));
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

    bool is_enterprise = (strcmp(auth, WIFI_AUTH_TYPE_ENTERPRISE) == 0);
    ESP_LOGI(TAG, "WiFi loaded: %s (%s)", ssid, is_enterprise ? "Enterprise" : "PSK");

    if (is_enterprise) {
        if (strlen(identity) == 0) strcpy(identity, username);
        if (strlen(identity) == 0) strcpy(identity, ssid);
        wifi_mgr_save_network_ext(0, ssid, WIFI_AUTH_TYPE_ENTERPRISE,
                                   identity, username[0] ? username : identity, pass);
    } else {
        wifi_mgr_save_network(0, ssid, pass);
    }
    return true;
}

static void save_wifi_to_sd(void) { wifi_save_config_to_sd(); }

// ═══════════════════════════════════════════════════════════════
//  Lifecycle
// ═══════════════════════════════════════════════════════════════

bool AlbumApp::init()
{
    wifi_mgr_init();
    led_init(); led_async_start();

    // RTC wake → restore index and date
    bool rtc_wake = pc_hal_is_rtc_wake();
    int rtc_idx = 1, rtc_date = 0;
    PowerManager::load_rtc_ram(&rtc_idx, &rtc_date);
    if (rtc_wake) {
        ESP_LOGI(TAG, "RTC wake, ram idx=%d date=%d", rtc_idx, rtc_date);
    }

    _sd_mounted = sd_card_mount();

    int today = 0;
    {
        m5::rtc_date_t d;
        m5::rtc_time_t t;
        if (M5.Rtc.getDateTime(&d, &t) && d.year >= 2024 && d.year <= 2099)
            today = d.year * 10000 + d.month * 100 + d.date;
    }

    _slideshow.init(today);

    // Restore last-viewed index from RTC RAM (reset to 1 on cold boot)
    _slideshow.current_idx = rtc_idx;

    if (!_sd_mounted) {
        ESP_LOGI(TAG, "No SD card — single-image mode");
        if (rtc_wake) {
            _slideshow.fetch_and_show_one();
            _pm.go_to_sleep(_slideshow.current_idx, _slideshow.last_update_date, false);
        }
        return true;
    }

    // ── SD mode ──
    _slideshow.ensure_album_folder();

    // Restore date from config.txt (primary) or RTC RAM (fallback)
    {
        int cfg_date = _slideshow.read_index_date();
        if (cfg_date > 20240000)
            _slideshow.last_update_date = cfg_date;
        else if (rtc_date > 20240000)
            _slideshow.last_update_date = rtc_date;
        ESP_LOGI(TAG, "Update date: %d", _slideshow.last_update_date);
    }

    _slideshow.total_images = _slideshow.scan_folder_images();

    if (!rtc_wake) {
        // Button wake
        if (_slideshow.total_images == 0) {
            _slideshow.refresh_all_images();
        } else if (today > _slideshow.last_update_date) {
            ESP_LOGI(TAG, "New day, queuing download");
            _slideshow.dl_pending = true;
        } else if (today > 0 && today == _slideshow.last_update_date &&
                   _slideshow.total_images < SS_MAX_IMAGES) {
            ESP_LOGI(TAG, "Resume interrupted download (%d/%d)",
                     _slideshow.total_images, SS_MAX_IMAGES);
            _slideshow.dl_pending = true;
        }
    } else {
        // RTC wake: advance to next image
        if (_slideshow.total_images > 0) {
            _slideshow.current_idx = (_slideshow.current_idx % _slideshow.total_images) + 1;
            _slideshow.load_and_show(_slideshow.current_idx);
        }
        if (today > 0 && today > _slideshow.last_update_date) {
            _slideshow.refresh_all_images();
        } else if (today > 0 && today == _slideshow.last_update_date &&
                   _slideshow.total_images < SS_MAX_IMAGES) {
            _slideshow.dl_pending = true;
        }
        if (!_slideshow.dl_pending)
            _pm.go_to_sleep(_slideshow.current_idx, _slideshow.last_update_date, true);
    }

    return true;
}

void AlbumApp::deinit()
{
    _running = false;
    _slideshow.deinit();
    if (_sd_mounted) { sd_card_unmount(); _sd_mounted = false; }
}

void AlbumApp::start() { _running = true; }
void AlbumApp::stop()  { _running = false; }
void AlbumApp::refresh() { _slideshow.dl_pending = true; }

// ═══════════════════════════════════════════════════════════════
//  Update loop
// ═══════════════════════════════════════════════════════════════

void AlbumApp::update()
{
    if (!_running) return;

    wifi_state_t ws = wifi_mgr_get_state();
    if (ws == WIFI_STATE_AP_IDLE || ws == WIFI_STATE_AP_CFG) return;

    // Deferred download
    _slideshow.run_pending_download();

    if (_sd_mounted) {
        _slideshow.check_auto_advance();
        int today = 0;
        m5::rtc_date_t d; m5::rtc_time_t t;
        if (M5.Rtc.getDateTime(&d, &t) && d.year >= 2024 && d.year <= 2099)
            today = d.year * 10000 + d.month * 100 + d.date;
        _slideshow.check_daily_update(today);
    } else {
        uint64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - _slideshow._last_slide_ms >= (30ULL * 60 * 1000)) {
            _slideshow._last_slide_ms = now_ms;
            _slideshow.fetch_and_show_one();
        }
    }

    handle_buttons();

    // Sleep check
    if (_slideshow.dl_pending || _slideshow.dl_in_progress) return;
    if (spi_bus_get_owner() != SPI_OWNER_NONE) return;
    if (ws == WIFI_STATE_STA_CN) return;
    if (_pm.is_idle())
        _pm.go_to_sleep(_slideshow.current_idx, _slideshow.last_update_date, _sd_mounted);
}

// ═══════════════════════════════════════════════════════════════
//  Buttons
// ═══════════════════════════════════════════════════════════════

void AlbumApp::handle_buttons()
{
    uint64_t now = esp_timer_get_time() / 1000;
    if (now < _btn_busy_until) return;
    _btn_busy_until = 0;

    if (BTN_UP.isPressed() || BTN_DOWN.isPressed() || BTN_TOP.isPressed())
        _pm.mark_activity();

    // UP + DOWN → provisioning
    if (BTN_UP.isPressed() && BTN_DOWN.isPressed()) {
        _btn_busy_until = now + 1000;
        if (load_wifi_from_sd() && wifi_mgr_connect_sta(5000)) {
            set_dns();
            save_wifi_to_sd();
            ESP_LOGI(TAG, "Connected via SD wifi.txt");
        } else {
            ESP_LOGI(TAG, "UP+DOWN: AP provisioning");
            led_async_breath_forever(255, 200, 0);
            wifi_mgr_trigger_provisioning();
        }
        return;
    }

    if (_sd_mounted) {
        if (BTN_UP.wasClicked())     _slideshow.show_prev();
        if (BTN_DOWN.wasClicked())   _slideshow.show_next();
        if (BTN_TOP.wasHold()) {
            ESP_LOGI(TAG, "TOP hold: refresh");
            _slideshow.refresh_all_images();
        }
    } else {
        if (BTN_TOP.wasHold()) {
            ESP_LOGI(TAG, "TOP hold: refresh 1 image");
            _slideshow.fetch_and_show_one();
        }
    }
}
