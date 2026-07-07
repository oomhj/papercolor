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

// ── Internal HTTP context ────────────────────────────────────

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
    rename(tmp, path);  // atomic on FatFS
    return true;
}
