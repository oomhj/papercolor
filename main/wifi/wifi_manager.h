/**
 * PaperColor — WiFi Manager
 *
 * Unified WiFi handling for STA (router) and AP (hotspot) modes.
 * Config stored in NVS. Supports multiple saved networks.
 */

#pragma once

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// ── Configuration ────────────────────────────────────────────

#define WIFI_MAX_SSID_LEN     32
#define WIFI_MAX_PASS_LEN     64
#define WIFI_MAX_IDENTITY_LEN 64
#define WIFI_SAVED_NETS       3       // number of saved networks
#define WIFI_AP_SSID_PREFIX   "PaperColor-"

// Auth mode values stored in NVS (prefixed to avoid collision with
// ESP-IDF's wifi_auth_mode_t enum which defines e.g. WIFI_AUTH_ENTERPRISE)
#define WIFI_AUTH_TYPE_PSK        "psk"
#define WIFI_AUTH_TYPE_ENTERPRISE "enterprise"

// ── State ────────────────────────────────────────────────────

typedef enum {
    WIFI_STATE_OFF,         // WiFi not initialized
    WIFI_STATE_STA_CN,      // STA connecting
    WIFI_STATE_STA_OK,      // STA connected
    WIFI_STATE_STA_FAIL,    // STA connection failed
    WIFI_STATE_STA_LOST,    // Runtime disconnection (was OK, now lost)
    WIFI_STATE_AP_IDLE,     // AP hotspot active, waiting for client
    WIFI_STATE_AP_CFG,      // AP active, client is configuring
} wifi_state_t;

// ── Callback ─────────────────────────────────────────────────

typedef void (*wifi_event_cb_t)(wifi_state_t old_state, wifi_state_t new_state);

// ── API ──────────────────────────────────────────────────────

/** @brief Init WiFi subsystem and NVS. Call once at boot. */
void wifi_mgr_init(void);

/** @brief Register a state-change callback. */
void wifi_mgr_on_event(wifi_event_cb_t cb);

/**
 * @brief Connect to a saved network (highest priority first).
 * @param timeout_ms  Max time to wait before giving up.
 * @return true if connected.
 */
bool wifi_mgr_connect_sta(uint32_t timeout_ms);

/** @brief Disconnect STA. */
void wifi_mgr_disconnect_sta(void);

/** @brief Start AP hotspot for provisioning. */
void wifi_mgr_start_ap(void);

/** @brief Stop AP hotspot. */
void wifi_mgr_stop_ap(void);

/** @brief Save a WiFi network to NVS at the given priority slot. */
bool wifi_mgr_save_network(int slot, const char* ssid, const char* pass);

/** @brief Read a saved network from NVS. Returns false if slot empty. */
bool wifi_mgr_load_network(int slot, char* ssid, size_t ssid_sz, char* pass, size_t pass_sz);

/**
 * @brief Save PSK or Enterprise network config to NVS.
 *        @p auth is "psk" or "enterprise". For PSK, identity/username are ignored.
 */
bool wifi_mgr_save_network_ext(int slot, const char* ssid, const char* auth,
                                const char* identity, const char* username,
                                const char* pass);

/**
 * @brief Load enterprise auth params for a slot.
 *        Returns false if slot empty or auth is not "enterprise".
 */
bool wifi_mgr_load_enterprise_params(int slot, char* identity, size_t identity_sz,
                                      char* username, size_t username_sz,
                                      char* pass, size_t pass_sz);

/**
 * @brief Read auth mode for a saved network slot.
 *        Writes "psk" or "enterprise". Returns false if slot empty.
 */
bool wifi_mgr_get_network_auth(int slot, char* auth, size_t auth_sz);

/** @brief Erase all saved WiFi configs from NVS. */
void wifi_mgr_erase_all(void);

/** @brief Get current WiFi state. */
wifi_state_t wifi_mgr_get_state(void);

/** @brief Get STA IP address string. Returns "0.0.0.0" if not connected. */
const char* wifi_mgr_get_ip(void);

/** @brief Get AP SSID string. */
const char* wifi_mgr_get_ap_ssid(void);

/** @brief Set LED according to current state. */
void wifi_mgr_update_led(void);

// ── Retry & Provisioning ────────────────────────────────────

/**
 * @brief Start retry loop: tries saved networks with backoff.
 * After max retries, auto-starts AP provisioning.
 * @param is_reconnect  true if called from a runtime disconnect (faster retry).
 */
void wifi_mgr_start_retry_loop(bool is_reconnect);

/** @brief Stop the retry loop (e.g. user pressed button). */
void wifi_mgr_stop_retry(void);

/** @brief Manually trigger AP provisioning (long-press callback). */
void wifi_mgr_trigger_provisioning(void);

/** @brief Restart the ESP. */
void wifi_mgr_restart(void) __attribute__((noreturn));

/**
 * @brief Call from main loop. Returns true if provisioning was triggered.
 *        Detects: BTN-A long press (3s) → trigger provisioning.
 *        BTN-C long press (5s) → deep sleep.
 */
bool wifi_mgr_handle_buttons(void);

#ifdef __cplusplus
}
#endif
