/**
 * PaperColor — WiFi Provisioning Server
 *
 * Captive portal with DNS hijack + HTTP config page.
 * Run after wifi_mgr_start_ap() for phone-based WiFi setup.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the provisioning server (DNS + HTTP).
 * Must be called AFTER wifi_mgr_start_ap().
 */
void wifi_prov_start(void);

/**
 * @brief Stop the provisioning server.
 */
void wifi_prov_stop(void);

/**
 * @brief Tick the provisioning server (call periodically).
 * Handles AP timeout, user activity detection.
 */
void wifi_prov_tick(void);

#ifdef __cplusplus
}
#endif
