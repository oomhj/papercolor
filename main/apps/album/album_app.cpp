/**
 * PaperColor — Network Photo Album
 *
 * Fetches random scenery images from API, displays on EPD.
 * BTN_TOP: refresh  |  BTN_TOP hold: sleep
 */
#include "album_app.h"
#include "hal/hal.h"
#include "hal/led_driver.h"
#include "wifi_manager.h"
#include "filter.h"
#include "hal/sd_card.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

static const char* TAG = "Album";

// ── Image API ─────────────────────────────────────────────────
static const char* IMAGE_URL = "https://bing.img.run/rand_1366x768.php";

// ── WiFi ──────────────────────────────────────────────────────
#ifndef ALBUM_SSID
#define ALBUM_SSID "Jason-home"
#endif
#ifndef ALBUM_PASS
#define ALBUM_PASS "admin1234"
#endif

static bool wifi_connect()
{
    ESP_LOGI(TAG, "WiFi connecting to SSID: \"%s\" (pass len: %zu)",
             ALBUM_SSID, strlen(ALBUM_PASS));

    static bool inited = false;
    if (!inited) {
        // ── NVS ──
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS needs erase (0x%x)", e);
            nvs_flash_erase();
            e = nvs_flash_init();
        }
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(e));
            return false;
        }
        ESP_LOGD(TAG, "NVS init OK");

        // ── Netif ──
        esp_err_t netif_e = esp_netif_init();
        if (netif_e != ESP_OK && netif_e != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(netif_e));
            return false;
        }
        esp_err_t evt_e = esp_event_loop_create_default();
        if (evt_e != ESP_OK && evt_e != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(evt_e));
            return false;
        }
        esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
        if (!sta_netif) {
            ESP_LOGE(TAG, "create default wifi sta failed");
            return false;
        }
        ESP_LOGD(TAG, "WiFi netif created");

        // ── WiFi Init ──
        wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t wif_e = esp_wifi_init(&wcfg);
        if (wif_e != ESP_OK && wif_e != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(wif_e));
            return false;
        }

        // ── Set STA mode ──
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t wc = {};
        strcpy((char*)wc.sta.ssid, ALBUM_SSID);
        strcpy((char*)wc.sta.password, ALBUM_PASS);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        esp_err_t cfg_e = esp_wifi_set_config(WIFI_IF_STA, &wc);
        if (cfg_e != ESP_OK) {
            ESP_LOGE(TAG, "set config failed: %s", esp_err_to_name(cfg_e));
            return false;
        }
        ESP_LOGI(TAG, "WiFi configured: %s", ALBUM_SSID);

        // ── Start ──
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
        inited = true;
    } else {
        // Already initialized — check if still connected
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi already connected to %s", ap.ssid);
            return true;
        }
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
    }

    // ── Connect (or reconnect) ──
    ESP_LOGI(TAG, "WiFi started, connecting...");
    esp_err_t conn_e = esp_wifi_connect();
    if (conn_e != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect returned: %s", esp_err_to_name(conn_e));
        // continue anyway — connection may still happen
    }

    // ── Wait for connection ──
    uint32_t deadline = esp_timer_get_time() / 1000 + 25000;
    int poll_count = 0;
    while (esp_timer_get_time() / 1000 < deadline) {
        wifi_ap_record_t ap;
        esp_err_t r = esp_wifi_sta_get_ap_info(&ap);
        if (r == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected — SSID: %s, RSSI: %d, Channel: %d, Auth: %d",
                     ap.ssid, ap.rssi, ap.primary, ap.authmode);
            ESP_LOGD(TAG, "  Signal: %d dBm (%s), BW: %d",
                     ap.rssi,
                     ap.rssi < -80 ? "weak" : ap.rssi < -67 ? "ok" : "strong",
                     ap.bandwidth);

            // Wait for IP + DNS from DHCP
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (netif) {
                for (int i = 0; i < 50; i++) {  // up to 10s
                    esp_netif_ip_info_t ip;
                    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
                        ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR(&ip.ip));
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
                vTaskDelay(pdMS_TO_TICKS(500));  // settle
                // Set DNS
                esp_netif_dns_info_t dns{};
                dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("114.114.114.114");
                dns.ip.type = ESP_IPADDR_TYPE_V4;
                esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
                ESP_LOGI(TAG, "  DNS: 114.114.114.114");
            }
            return true;
        }
        if (poll_count % 25 == 0) {  // every ~5 seconds
            ESP_LOGI(TAG, "  waiting for WiFi... (%d/%dms, err: %s)",
                     (int)(esp_timer_get_time() / 1000) - (int)(deadline - 25000),
                     25000, esp_err_to_name(r));
        }
        poll_count++;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // ── Timeout ──
    ESP_LOGE(TAG, "WiFi timeout after 25s — no AP info received");
    ESP_LOGE(TAG, "  Check: SSID \"%s\" exists?  Password correct?  2.4GHz only?",
             ALBUM_SSID);
    ESP_LOGE(TAG, "  Tips: enable WiFi event handler or check router logs for details");
    return false;
}

// ── HTTP fetch (bypass DNS, use IP + Host header) ─────────────

static bool http_fetch_image(const char* url,
                              uint8_t** out_buf, size_t* out_len)
{
    *out_buf = nullptr; *out_len = 0;

    ESP_LOGI(TAG, "HTTP fetch start");
    ESP_LOGI(TAG, "  URL: %s", url);
    ESP_LOGI(TAG, "  Timeout: 30000ms, Buffer: 8192");

    // ── DNS resolve (debug: resolve URL's domain name) ──
    {
        // Extract hostname from URL for debugging
        const char* domain_start = strstr(url, "://");
        if (domain_start) {
            domain_start += 3;
            const char* domain_end = strchr(domain_start, '/');
            if (!domain_end) domain_end = domain_start + strlen(domain_start);
            // Cap at 128 chars
            char domain[128];
            size_t dlen = (size_t)(domain_end - domain_start);
            if (dlen > sizeof(domain) - 1) dlen = sizeof(domain) - 1;
            memcpy(domain, domain_start, dlen);
            domain[dlen] = '\0';

            struct addrinfo hints = {};
            struct addrinfo* res = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            int dns_ret = getaddrinfo(domain, nullptr, &hints, &res);
            if (dns_ret == 0 && res) {
                char ip_str[64] = {};
                if (res->ai_family == AF_INET) {
                    struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
                    inet_ntop(AF_INET, &sa->sin_addr, ip_str, sizeof(ip_str));
                } else if (res->ai_family == AF_INET6) {
                    struct sockaddr_in6* sa6 = (struct sockaddr_in6*)res->ai_addr;
                    inet_ntop(AF_INET6, &sa6->sin6_addr, ip_str, sizeof(ip_str));
                }
                ESP_LOGI(TAG, "  DNS: %s → %s", domain, ip_str);
                freeaddrinfo(res);
            } else {
                ESP_LOGW(TAG, "  DNS: %s resolution failed (gai: %d)", domain, dns_ret);
            }
        }
    }

    // ── HTTP client setup ──
    esp_http_client_config_t cfg = {};
    cfg.url                       = url;
    cfg.timeout_ms                = 30000;
    cfg.buffer_size               = 8192;
    cfg.buffer_size_tx            = 1024;
    cfg.crt_bundle_attach         = esp_crt_bundle_attach;
    cfg.method                    = HTTP_METHOD_GET;
    cfg.max_redirection_count     = 5;
    cfg.skip_cert_common_name_check = false;

    // Body accumulator — allocate in DRAM (avoids PSRAM read issues in drawJpg)
    struct { uint8_t* buf; size_t len; size_t cap; } acc = {};
    cfg.user_data = &acc;

    cfg.event_handler = [](esp_http_client_event_t* evt) -> esp_err_t {
        auto* a = (decltype(acc)*)evt->user_data;
        if (evt->event_id == HTTP_EVENT_ON_HEADER) {
            ESP_LOGI(TAG, "  Response header: %s: %s",
                     evt->header_key, evt->header_value);
        }
        if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
            size_t needed = a->len + evt->data_len + 4096;
            if (needed > a->cap) {
                // Try DRAM first, fall back to PSRAM
                uint8_t* nb = (uint8_t*)heap_caps_realloc(
                    a->buf, needed, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (!nb) {
                    nb = (uint8_t*)realloc(a->buf, needed);
                }
                if (!nb) {
                    ESP_LOGE(TAG, "OOM accumulating body (%zu bytes)", needed);
                    return ESP_FAIL;
                }
                a->buf = nb;
                a->cap = needed;
            }
            memcpy(a->buf + a->len, evt->data, evt->data_len);
            a->len += evt->data_len;
        }
        return ESP_OK;
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGE(TAG, "http client init failed — OOM?");
        return false;
    }
    ESP_LOGD(TAG, "HTTP client initialized");

    // ── Request headers ──
    esp_http_client_set_header(c, "User-Agent",
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/149.0.0.0 Safari/537.36");
    esp_http_client_set_header(c, "Cache-Control", "no-cache");
    ESP_LOGI(TAG, "Headers: UA + Cache-Control set");

    // ── Perform (connect + send + receive headers) with retry ──
    esp_err_t err;
    int elapsed_ms = 0;
    for (int retry = 0; retry < 2; retry++) {
        if (retry) {
            ESP_LOGW(TAG, "  Retry %d...", retry);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        uint32_t t0 = esp_timer_get_time() / 1000;
        err = esp_http_client_perform(c);
        uint32_t t1 = esp_timer_get_time() / 1000;
        int elapsed_ms = t1 - t0;
        if (err == ESP_OK) break;
        ESP_LOGE(TAG, "HTTP perform FAILED after %dms", elapsed_ms);
        ESP_LOGE(TAG, "  Error: %s (0x%x)", esp_err_to_name(err), err);
        if (err == ESP_ERR_HTTP_CONNECT) {
            ESP_LOGE(TAG, "  → TCP connection refused / timed out — server unreachable?");
        } else if (err == ESP_ERR_HTTP_WRITE_DATA) {
            ESP_LOGE(TAG, "  → Failed to send request headers");
        } else if (err == ESP_ERR_HTTP_FETCH_HEADER) {
            ESP_LOGE(TAG, "  → Response header parse error");
        } else if (err == ESP_ERR_HTTP_INVALID_TRANSPORT) {
            ESP_LOGE(TAG, "  → Invalid transport (TLS issue?)");
        } else if (err == ESP_ERR_HTTP_CONNECTING) {
            ESP_LOGE(TAG, "  → Netconn open failed");
        } else if (err == ESP_ERR_HTTP_EAGAIN) {
            ESP_LOGE(TAG, "  → Resource temporarily unavailable");
        }
    }
    if (err != ESP_OK) {
        esp_http_client_cleanup(c);
        return false;
    }

    // ── Response metadata ──
    int status = esp_http_client_get_status_code(c);
    int64_t len = esp_http_client_get_content_length(c);
    char url_buf[256] = {};
    esp_http_client_get_url(c, url_buf, sizeof(url_buf));

    ESP_LOGI(TAG, "HTTP response — %s", url_buf);
    ESP_LOGI(TAG, "  Status: %d  |  Size: %lld bytes  |  Time: %dms",
             status, (long long)len, elapsed_ms);

    // Body already accumulated in acc via HTTP_EVENT_ON_DATA during perform()
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP error %d (expected 200)", status);
        if (status == 301 || status == 302 || status == 307 || status == 308) {
            ESP_LOGE(TAG, "  Redirect not followed — check max_redirection_count / URL");
        } else if (status == 403) {
            ESP_LOGE(TAG, "  Forbidden — server rejected request");
        } else if (status == 404) {
            ESP_LOGE(TAG, "  Not found — check URL path");
        } else if (status >= 500) {
            ESP_LOGE(TAG, "  Server error");
        }
        if (acc.buf) free(acc.buf);
        esp_http_client_cleanup(c);
        return false;
    }

    ESP_LOGI(TAG, "Body accumulated: %zu bytes", acc.len);
    if (acc.len == 0) {
        ESP_LOGE(TAG, "Empty response body — server returned no data");
        if (acc.buf) free(acc.buf);
        esp_http_client_cleanup(c);
        return false;
    }

    // Scan past redirect body to find JPEG/PNG data
    size_t skip = 0;
    for (size_t i = 0; i + 1 < acc.len; i++) {
        if (acc.buf[i] == 0xFF && acc.buf[i+1] == 0xD8) { skip = i; break; }
    }
    if (skip > 0) {
        ESP_LOGW(TAG, "  Skipped %zu bytes of redirect body before JPEG", skip);
        acc.len -= skip;
        memmove(acc.buf, acc.buf + skip, acc.len);
    }
    ESP_LOGI(TAG, "  JPEG header: OK (FF D8) — %zu bytes", acc.len);

    esp_http_client_cleanup(c);
    *out_buf = acc.buf;
    *out_len = acc.len;
    return true;
}

// ── Lifecycle ────────────────────────────────────────────────

bool AlbumApp::init() {
    _img_buf = nullptr; _img_len = 0;
    _decoded_buf = nullptr; _decoded_sw = _decoded_sh = 0;
    _decoded_crop_x = _decoded_out_y = 0;
    _filter_idx = 0;  // default: None
    wifi_mgr_init();
    led_init(); led_async_start();

    // Test SD card mount
    if (sd_card_mount()) {
        ESP_LOGI(TAG, "SD card mounted OK");
        sd_card_unmount();
    } else {
        ESP_LOGW(TAG, "SD card mount failed (no card?)");
    }

    return true;
}

void AlbumApp::deinit()
{
    _running = false;
    if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
    if (_decoded_buf) { free(_decoded_buf); _decoded_buf = nullptr; }
}

void AlbumApp::start() { _running = true; _needs_refresh = true; }
void AlbumApp::stop()  { _running = false; }
void AlbumApp::refresh() { _needs_refresh = true; }

void AlbumApp::update()
{
    if (!_running) return;

    if (_needs_refresh) {
        _needs_refresh = false;
        _needs_rerender = false;
        if (_decoded_buf) { free(_decoded_buf); _decoded_buf = nullptr; }
        ESP_LOGI(TAG, "--- Refresh triggered ---");

        bool ok = false;

        led_async_breath_forever(255, 165, 0);   // orange → WiFi
        uint32_t t_wifi = esp_timer_get_time() / 1000;

        if (wifi_connect()) {
            uint32_t t_fetch = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "WiFi OK (%dms), starting HTTP fetch", (int)(t_fetch - t_wifi));

            led_async_breath_forever(0, 0, 255); // blue → HTTP (replaces orange)
            if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
            ok = http_fetch_image(IMAGE_URL, &_img_buf, &_img_len);

            uint32_t t_done = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "HTTP fetch %s (%dms)",
                     ok ? "SUCCESS" : "FAILED",
                     (int)(t_done - t_fetch));
        } else {
            ESP_LOGE(TAG, "WiFi FAILED — skipping HTTP fetch");
        }

        if (ok) {
            led_async_flash(0, 255, 0, 3);       // green 3×: image ready, start render
            render();
        } else {
            led_async_flash(255, 0, 0, 3);       // red 3× (replaces blue/orange)
        }
    } else if (_needs_rerender && _decoded_buf) {
        _needs_rerender = false;
        ESP_LOGI(TAG, "--- Filter switch ---");
        render();
    }
    handle_buttons();
}

void AlbumApp::render()
{
    const int w = g_canvas->width();
    const int h = g_canvas->height();

    if (_img_buf || _decoded_buf) {
        uint32_t t0 = esp_timer_get_time() / 1000;

        g_canvas->fillScreen(TFT_WHITE);
        bool ok = false;

        // Step 1: decode JPEG → RGB565_BE if not cached
        if (_img_buf && !_decoded_buf) {
            jpeg_dec_handle_t jdec = NULL;
            jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
            cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;

            jpeg_dec_header_info_t hdr = {};
            {
                jpeg_dec_io_t io = {}; io.inbuf = _img_buf; io.inbuf_len = (int)_img_len;
                bool ok_header = false;
                if (jpeg_dec_open(&cfg, &jdec) == JPEG_ERR_OK) {
                    ok_header = (jpeg_dec_parse_header(jdec, &io, &hdr) == JPEG_ERR_OK);
                    jpeg_dec_close(jdec); jdec = NULL;
                }
                if (!ok_header) { ok = false; goto render_done; }
            }

            int sh = 400;
            int sw = (hdr.width * sh + hdr.height / 2) / hdr.height;
            sw = ((sw + 4) / 8) * 8;
            if (sw < w) sw = w;
            int crop_x = (sw > w) ? (sw - w) / 2 : 0;
            int out_y  = (h - sh) / 2;

            cfg.scale.width = sw;
            cfg.scale.height = sh;
            if (jpeg_dec_open(&cfg, &jdec) == JPEG_ERR_OK) {
                jpeg_dec_io_t io = {}; io.inbuf = _img_buf; io.inbuf_len = (int)_img_len;
                jpeg_dec_parse_header(jdec, &io, &hdr);
                int out_len = 0;
                jpeg_dec_get_outbuf_len(jdec, &out_len);
                if (out_len > 0) {
                    uint8_t* out = (uint8_t*)jpeg_calloc_align(out_len, 16);
                    if (out) {
                        io.outbuf = out; io.out_size = out_len;
                        if (jpeg_dec_process(jdec, &io) == JPEG_ERR_OK) {
                            _decoded_buf = out;
                            _decoded_sw = sw;
                            _decoded_sh = sh;
                            _decoded_crop_x = crop_x;
                            _decoded_out_y = out_y;
                            ok = true;
                        } else {
                            jpeg_free_align(out);
                        }
                    }
                }
                jpeg_dec_close(jdec);
            }
            if (_img_buf) { free(_img_buf); _img_buf = nullptr; _img_len = 0; }
        } else if (_decoded_buf) {
            ok = true;
        }

render_done:
        // Step 2: apply filter → push to canvas
        if (ok && _decoded_buf) {
            const filter_t* f = &FILTERS[_filter_idx];
            if (f->fn) {
                uint8_t* dithered = (uint8_t*)malloc(w * h);
                if (dithered) {
                    int sw = _decoded_sw;
                    const uint16_t* src = (const uint16_t*)_decoded_buf
                                          + _decoded_out_y * sw + _decoded_crop_x;
                    uint16_t* crop = (uint16_t*)malloc(w * h * 2);
                    if (crop) {
                        for (int y = 0; y < h; y++)
                            memcpy(crop + y * w, src + y * sw, w * 2);
                        f->fn(crop, dithered, w, h);
                        g_canvas->pushImage(0, 0, w, h, dithered);
                        free(crop);
                    }
                    free(dithered);
                }
            } else {
                int sw = _decoded_sw;
                const uint16_t* src = (const uint16_t*)_decoded_buf
                                      + _decoded_out_y * sw + _decoded_crop_x;
                for (int y = 0; y < h; y++)
                    g_canvas->pushImage(0, y, w, 1, src + y * sw);
            }
        }

        // Fallback
        if (!ok && _img_buf) {
            ESP_LOGW(TAG, "trying M5GFX drawJpg fallback...");
            M5Canvas tmp(g_canvas);
            tmp.setColorDepth(16);
            if (tmp.createSprite(w, h)) {
                ok = tmp.drawJpg(_img_buf, _img_len, 0, 0, w, h, 0, 0, 1.0f);
                if (ok) tmp.pushSprite(g_canvas, 0, 0);
                tmp.deleteSprite();
            }
        }

        uint32_t t1 = esp_timer_get_time() / 1000;
        ESP_LOGI(TAG, "filter[%s]: %s (%dms)",
                 FILTERS[_filter_idx].name,
                 ok ? "OK" : "FAIL",
                 (int)(t1 - t0));
        if (!ok) {
            g_canvas->setTextColor(TFT_RED);
            g_canvas->setFont(&fonts::Font4);
            g_canvas->drawString("Decode Failed", (w - 140) / 2, h / 2 - 14);
            ESP_LOGE(TAG, "all decode attempts failed");
        }
    } else {
        g_canvas->fillScreen(TFT_BLACK);
        g_canvas->setTextColor(TFT_WHITE);
        g_canvas->setFont(&fonts::Font4);
        g_canvas->drawString("Load Failed", (w-160)/2, h/2-14);
    }

    uint32_t t2 = esp_timer_get_time() / 1000;
    pc_hal_display();
    uint32_t t3 = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "display: pushed to EPD (%dms)", (int)(t3 - t2));
}

void AlbumApp::handle_buttons()
{
    if (BTN_TOP.wasClicked()) {          // Top button → refresh image
        ESP_LOGI(TAG, "BTN_TOP: refresh");
        _needs_refresh = true;
    }
    if (BTN_TOP.wasHold()) {             // Long press → provisioning
        ESP_LOGI(TAG, "BTN_TOP hold: provisioning");
        wifi_mgr_trigger_provisioning();
    }
}
