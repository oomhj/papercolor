/**
 * PaperColor — Image Downloader Implementation
 */

#include "image_downloader.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <sys/stat.h>

static const char* TAG = "DL";

// ── Server Date header ───────────────────────────────────────
static char s_server_date[64] = {0};  // raw "Date" header value (RFC 2822)
static char s_server_iso[24] = {0};   // parsed "YYYY-MM-DDTHH:MM:SSZ"
static bool s_server_date_ok = false;

/**
 * Parse RFC 2822 date string ("Wed, 08 Jul 2026 03:30:00 GMT")
 * into ISO format YYYY-MM-DDTHH:MM:SSZ (UTC).
 */
static void parse_server_date(const char* raw)
{
    if (!raw || !raw[0]) return;
    // Skip optional weekday prefix, e.g. "Wed, "
    const char* p = raw;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Expect: DD Mon YYYY HH:MM:SS ...
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char mon_str[8] = {0};
    if (sscanf(p, "%d %3s %d %d:%d:%d", &day, mon_str, &year, &hh, &mm, &ss) < 5) return;

    static const char* months[12] = {"Jan","Feb","Mar","Apr","May","Jun",
                                      "Jul","Aug","Sep","Oct","Nov","Dec"};
    int mon = -1;
    for (int i = 0; i < 12; i++) {
        if (strcasecmp(mon_str, months[i]) == 0) { mon = i + 1; break; }
    }
    if (mon < 1) return;

    snprintf(s_server_iso, sizeof(s_server_iso),
             "%04d-%02d-%02dT%02d:%02d:%02dZ", year, mon, day, hh, mm, ss);
    s_server_date_ok = true;
    ESP_LOGI(TAG, "Server time: %s", s_server_iso);
}

// ── Internal HTTP context ────────────────────────────────────

struct http_ctx { uint8_t* buf; size_t len; size_t cap; };

static esp_err_t http_event_handler(esp_http_client_event_t* evt)
{
    auto* ctx = (http_ctx*)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp((const char*)evt->header_key, "date") == 0) {
            size_t n = strlen((const char*)evt->header_value);
            if (n >= sizeof(s_server_date)) n = sizeof(s_server_date) - 1;
            memcpy(s_server_date, evt->header_value, n);
            s_server_date[n] = '\0';
            s_server_date_ok = false;
        }
    }
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
    if (evt->event_id == HTTP_EVENT_ON_FINISH && s_server_date[0]) {
        parse_server_date(s_server_date);
    }
    return ESP_OK;
}

// ── Fetch ────────────────────────────────────────────────────

bool dl_fetch(const char* url, uint8_t** out, size_t* out_len)
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

    // Reject oversized payload
    if (acc.len > DL_MAX_JPEG_BYTES) { free(acc.buf); return false; }

    // Find JPEG start marker
    size_t skip = 0;
    for (size_t i = 0; i + 1 < acc.len; i++)
        if (acc.buf[i] == 0xFF && acc.buf[i+1] == 0xD8) { skip = i; break; }
    if (skip > 0) { acc.len -= skip; memmove(acc.buf, acc.buf + skip, acc.len); }

    if (acc.len < 2 || acc.buf[0] != 0xFF || acc.buf[1] != 0xD8) { free(acc.buf); return false; }

    *out = acc.buf;
    *out_len = acc.len;
    return true;
}

bool dl_fetch_default(uint8_t** out, size_t* out_len)
{
    return dl_fetch(DL_DEFAULT_URL, out, out_len);
}

// ── Save ─────────────────────────────────────────────────────

bool dl_save(const char* album_dir, int index, const uint8_t* jpeg, size_t len)
{
    char path[64], tmp[64];
    snprintf(path, sizeof(path), "%s/%d.jpg", album_dir, index);
    snprintf(tmp,  sizeof(tmp),  "%s/%d.tmp", album_dir, index);

    ESP_LOGI(TAG, "save %s (%zu bytes)", path, len);

    FILE* f = fopen(tmp, "w");
    if (!f) return false;
    size_t written = fwrite(jpeg, 1, len, f);
    fclose(f);

    if (written != len) { remove(tmp); return false; }

    // FatFS rename() does NOT overwrite an existing target file.
    // Remove the old .jpg first, then rename .tmp → .jpg.
    // If power is lost between remove and rename, the .tmp survives
    // and scan_folder_images() will detect a missing file on next boot.
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

const char* dl_last_date(void)
{
    return s_server_date_ok ? s_server_iso : NULL;
}
