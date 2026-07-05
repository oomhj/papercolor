/**
 * PaperColor — WiFi Manager Implementation
 *
 * STA + AP modes with NVS-backed config storage.
 * LED indication integrated for each state.
 */

#include "wifi_manager.h"
#include "wifi_provisioning.h"
#include "hal/button.h"
#include "hal/led_driver.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <M5Unified.h>

static const char* TAG = "WiFiMgr";

// ── State ────────────────────────────────────────────────────

static wifi_state_t    s_state          = WIFI_STATE_OFF;
static wifi_event_cb_t s_callback       = nullptr;
static char            s_ip_str[16]     = "0.0.0.0";
static char            s_ap_ssid[32]    = "";
static bool            s_nvs_inited     = false;
static bool            s_netif_inited   = false;
// ── LED managed via led_driver async API ─────────────────────

// ── State management ─────────────────────────────────────────

static void set_state(wifi_state_t new_state)
{
    wifi_state_t old = s_state;
    if (old == new_state) return;
    s_state = new_state;
    ESP_LOGI(TAG, "state: %d -> %d", old, new_state);
    wifi_mgr_update_led();
    if (s_callback) s_callback(old, new_state);
}

void wifi_mgr_update_led(void)
{
    switch (s_state) {
        case WIFI_STATE_STA_CN:
            led_async_breath_forever(0, 0, 255);        break;  // blue breathing
        case WIFI_STATE_STA_OK:
            led_async_color(0, 255, 0);                 break;  // green
        case WIFI_STATE_STA_FAIL:
            led_async_flash(255, 0, 0, 3);              break;  // red flash 3×
        case WIFI_STATE_STA_LOST:
            led_async_flash(255, 100, 0, 10);           break;  // orange flash
        case WIFI_STATE_AP_IDLE:
            led_async_flash_forever(255, 255, 0);       break;  // yellow flash
        case WIFI_STATE_AP_CFG:
            led_async_color(0, 100, 255);               break;  // cyan
        default:
            led_async_stop();                           break;
    }
}

// ── Event handler ────────────────────────────────────────────

static void event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "STA connected");
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "STA disconnected");
        if (s_state == WIFI_STATE_STA_OK) {
            set_state(WIFI_STATE_STA_LOST);  // runtime loss
        }
        if (s_state == WIFI_STATE_STA_CN) {
            set_state(WIFI_STATE_STA_FAIL);  // connection failed
        }
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = (ip_event_got_ip_t*)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        set_state(WIFI_STATE_STA_OK);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "AP client connected");
        if (s_state == WIFI_STATE_AP_IDLE) set_state(WIFI_STATE_AP_CFG);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "AP client disconnected");
        if (s_state == WIFI_STATE_AP_CFG) set_state(WIFI_STATE_AP_IDLE);
    }
}

// ── Init ─────────────────────────────────────────────────────

void wifi_mgr_init(void)
{
    // NVS
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    s_nvs_inited = (e == ESP_OK);

    // Netif + event loop
    if (esp_netif_init() == ESP_OK || esp_netif_init() == ESP_ERR_INVALID_STATE)
        s_netif_inited = true;
    if (esp_event_loop_create_default() != ESP_OK &&
        esp_event_loop_create_default() != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop failed");
    }

    // Create netifs (AP + STA)
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // Register handlers
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    // Init WiFi
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wcfg) == ESP_OK || esp_wifi_init(&wcfg) == ESP_ERR_INVALID_STATE) {
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_start();
    }

    // Build AP SSID
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X%02X",
             WIFI_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "init done, AP SSID: %s", s_ap_ssid);
    set_state(WIFI_STATE_OFF);
}

// ── Callback ─────────────────────────────────────────────────

void wifi_mgr_on_event(wifi_event_cb_t cb) { s_callback = cb; }

// ── STA Connect ──────────────────────────────────────────────

bool wifi_mgr_connect_sta(uint32_t timeout_ms)
{
    esp_wifi_set_mode(WIFI_MODE_STA);

    // Try each saved network until one works
    for (int slot = 0; slot < WIFI_SAVED_NETS; slot++) {
        char ssid[WIFI_MAX_SSID_LEN] = {};
        char pass[WIFI_MAX_PASS_LEN] = {};
        if (!wifi_mgr_load_network(slot, ssid, sizeof(ssid), pass, sizeof(pass)))
            continue;

        ESP_LOGI(TAG, "Trying slot %d: %s (pass: %s)", slot, ssid, pass[0] ? pass : "<none>");
        wifi_config_t wc = {};
        strcpy((char*)wc.sta.ssid, ssid);
        if (pass[0]) strcpy((char*)wc.sta.password, pass);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_start();
        esp_wifi_connect();

        set_state(WIFI_STATE_STA_CN);

        uint32_t deadline = esp_timer_get_time() / 1000 + timeout_ms;
        while (esp_timer_get_time() / 1000 < deadline) {
            if (s_state == WIFI_STATE_STA_OK) return true;
            if (s_state == WIFI_STATE_STA_FAIL) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGW(TAG, "Slot %d failed", slot);
    }

    set_state(WIFI_STATE_STA_FAIL);
    return false;
}

void wifi_mgr_disconnect_sta(void)
{
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_NULL);
    set_state(WIFI_STATE_OFF);
}

// ── AP Mode ──────────────────────────────────────────────────

void wifi_mgr_start_ap(void)
{
    esp_wifi_stop();                                         // stop first (may be in STA/NULL mode)
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t ap = {};
    strcpy((char*)ap.ap.ssid, s_ap_ssid);
    ap.ap.max_connection = 4;
    ap.ap.channel        = 6;
    ap.ap.ssid_len       = (uint8_t)strlen(s_ap_ssid);
    ap.ap.authmode       = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
    set_state(WIFI_STATE_AP_IDLE);
    ESP_LOGI(TAG, "AP started: %s", s_ap_ssid);
}

void wifi_mgr_stop_ap(void)
{
    esp_wifi_set_mode(WIFI_MODE_NULL);
    if (s_state == WIFI_STATE_AP_IDLE || s_state == WIFI_STATE_AP_CFG)
        set_state(WIFI_STATE_OFF);
}

// ── NVS Storage ──────────────────────────────────────────────

bool wifi_mgr_save_network(int slot, const char* ssid, const char* pass)
{
    if (slot < 0 || slot >= WIFI_SAVED_NETS || !ssid) return false;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return false;

    char key_ssid[16], key_pass[16];
    snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", slot);
    snprintf(key_pass, sizeof(key_pass), "pass_%d", slot);

    nvs_set_str(h, key_ssid, ssid);
    nvs_set_str(h, key_pass, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved slot %d: %s", slot, ssid);
    return true;
}

bool wifi_mgr_load_network(int slot, char* ssid, size_t ssid_sz, char* pass, size_t pass_sz)
{
    if (slot < 0 || slot >= WIFI_SAVED_NETS) return false;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;

    char key_ssid[16], key_pass[16];
    snprintf(key_ssid, sizeof(key_ssid), "ssid_%d", slot);
    snprintf(key_pass, sizeof(key_pass), "pass_%d", slot);

    size_t len = ssid_sz;
    esp_err_t e = nvs_get_str(h, key_ssid, ssid, &len);
    if (e != ESP_OK) { nvs_close(h); return false; }

    len = pass_sz;
    nvs_get_str(h, key_pass, pass, &len);  // optional

    nvs_close(h);
    return true;
}

void wifi_mgr_erase_all(void)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

// ── Getters ──────────────────────────────────────────────────

wifi_state_t wifi_mgr_get_state(void) { return s_state; }
const char*  wifi_mgr_get_ip(void)    { return s_ip_str; }
const char*  wifi_mgr_get_ap_ssid(void) { return s_ap_ssid; }

// ═══════════════════════════════════════════════════════════════
//  Retry Loop
// ═══════════════════════════════════════════════════════════════

static TaskHandle_t s_retry_task = NULL;
static volatile bool s_retry_abort = false;

void wifi_mgr_stop_retry(void)
{
    s_retry_abort = true;
    if (s_retry_task) {
        vTaskDelete(s_retry_task);
        s_retry_task = NULL;
    }
}

static void retry_task_func(void* param)
{
    bool is_reconnect = (bool)param;

    // ── Retry parameters ──
    int max_attempts = is_reconnect ? 3 : 3;
    uint32_t timeouts[] = {10000, 10000, 15000};
    uint32_t delays[]   = {0, 5000, 15000};

    for (int attempt = 0; attempt < max_attempts && !s_retry_abort; attempt++) {
        ESP_LOGI(TAG, "Retry %d/%d", attempt + 1, max_attempts);
        set_state(WIFI_STATE_STA_CN);

        led_async_breath_forever(0, 0, 255);  // blue breath while waiting
        uint32_t wait_end = esp_timer_get_time() / 1000 +
                           ((attempt > 0) ? delays[attempt - 1] : 0);
        while (esp_timer_get_time() / 1000 < wait_end && !s_retry_abort) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        led_async_stop();
        if (s_retry_abort) break;

        // Try each saved slot
        bool ok = false;
        for (int slot = 0; slot < WIFI_SAVED_NETS && !ok && !s_retry_abort; slot++) {
            char ssid[WIFI_MAX_SSID_LEN] = {};
            char pass[WIFI_MAX_PASS_LEN] = {};
            if (!wifi_mgr_load_network(slot, ssid, sizeof(ssid), pass, sizeof(pass)))
                continue;

            ESP_LOGI(TAG, "  try slot %d: %s", slot, ssid);
            wifi_config_t wc = {};
            strcpy((char*)wc.sta.ssid, ssid);
            if (pass[0]) strcpy((char*)wc.sta.password, pass);
            esp_wifi_set_mode(WIFI_MODE_STA);
            esp_wifi_set_config(WIFI_IF_STA, &wc);
            esp_wifi_start();
            esp_wifi_connect();

            uint32_t deadline = esp_timer_get_time() / 1000 + timeouts[attempt];
            while (esp_timer_get_time() / 1000 < deadline && !s_retry_abort) {
                if (s_state == WIFI_STATE_STA_OK) { ok = true; break; }
                if (s_state == WIFI_STATE_STA_FAIL) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            if (ok || s_retry_abort) break;
        }

        if (ok) {
            ESP_LOGI(TAG, "Connected on attempt %d", attempt + 1);
            set_state(WIFI_STATE_STA_OK);
            wifi_mgr_start_ap();
            wifi_prov_start();
            // Schedule AP auto-stop after 3 min
            vTaskDelay(pdMS_TO_TICKS(180000));
            wifi_prov_stop();
            wifi_mgr_stop_ap();
            wifi_mgr_update_led();
            s_retry_task = NULL;
            vTaskDelete(NULL);
            return;
        }
    }

    // ── All attempts exhausted → start AP provisioning ──
    if (!s_retry_abort) {
        ESP_LOGW(TAG, "All retries failed, starting AP provisioning");
        wifi_mgr_trigger_provisioning();
    }

    s_retry_task = NULL;
    vTaskDelete(NULL);
}

void wifi_mgr_start_retry_loop(bool is_reconnect)
{
    wifi_mgr_stop_retry();
    s_retry_abort = false;
    xTaskCreate(retry_task_func, "wifi_retry", 4096,
                (void*)is_reconnect, 5, &s_retry_task);
}

// ═══════════════════════════════════════════════════════════════
//  Provisioning Trigger
// ═══════════════════════════════════════════════════════════════

void wifi_mgr_trigger_provisioning(void)
{
    wifi_mgr_stop_retry();

    // Disconnect STA if connected
    esp_wifi_disconnect();

    // Start AP + provisioning server
    wifi_mgr_start_ap();
    wifi_prov_start();

    ESP_LOGI(TAG, "Provisioning mode: AP=%s", s_ap_ssid);
}

void wifi_mgr_restart(void)
{
    ESP_LOGI(TAG, "Restarting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    while (1) {}
}

// ═══════════════════════════════════════════════════════════════
//  Button Handling
// ═══════════════════════════════════════════════════════════════

static uint32_t s_btn_up_pressed_ms = 0;
static bool     s_btn_up_triggered  = false;

bool wifi_mgr_handle_buttons(void)
{
    // BTN_TOP (G1): long press 3s → provisioning
    if (BTN_TOP.isPressed()) {
        if (!s_btn_up_pressed_ms) {
            s_btn_up_pressed_ms = esp_timer_get_time() / 1000;
            s_btn_up_triggered  = false;
            led_set_color(0, 0, 255);  // blue: preparing
        }
        uint32_t elapsed = (esp_timer_get_time() / 1000) - s_btn_up_pressed_ms;
        if (elapsed >= 3000 && !s_btn_up_triggered) {
            s_btn_up_triggered = true;
            led_set_color(0, 255, 0);  // green: confirmed
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI(TAG, "BTN_TOP long press: provisioning");
            wifi_mgr_trigger_provisioning();
            return true;
        }
    } else {
        s_btn_up_pressed_ms = 0;
    }

    return false;
}
