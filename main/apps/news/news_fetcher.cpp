/**
 * PaperColor — RSS HTTP Fetcher Implementation
 *
 * Uses open → fetch_headers → read pattern for chunked-encoding support.
 */
#include "news_fetcher.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "NewsFetch";

fetch_result_t news_fetch_url(const char* url, uint32_t timeout_ms)
{
    fetch_result_t result = {nullptr, 0, ESP_FAIL};

    esp_http_client_config_t cfg = {};
    cfg.url                       = url;
    cfg.timeout_ms                = timeout_ms;
    cfg.buffer_size               = 4096;
    cfg.buffer_size_tx            = 1024;
    cfg.keep_alive_enable         = false;
    cfg.crt_bundle_attach         = esp_crt_bundle_attach;
    cfg.method                    = HTTP_METHOD_GET;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return result;
    }

    // Open connection & send request
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        result.err = err;
        return result;
    }

    // Read headers
    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP %d, Content-Length: %lld", status, (long long)content_length);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status);
        esp_http_client_cleanup(client);
        result.err = ESP_FAIL;
        return result;
    }

    // Read body — works with both fixed-length and chunked
    size_t alloc = (content_length > 0) ? (size_t)content_length + 1 : 16384;
    char* buf = (char*)malloc(alloc);
    if (!buf) {
        ESP_LOGE(TAG, "OOM");
        esp_http_client_cleanup(client);
        return result;
    }

    size_t total = 0;
    int read_len;
    while ((read_len = esp_http_client_read(client, buf + total, alloc - total - 1)) > 0) {
        total += read_len;
        if (total + 512 >= alloc) {
            alloc *= 2;
            char* nb = (char*)realloc(buf, alloc);
            if (!nb) { free(buf); esp_http_client_cleanup(client); return result; }
            buf = nb;
        }
    }

    buf[total] = '\0';
    ESP_LOGI(TAG, "Fetched %zu bytes", total);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    result.data   = buf;
    result.length = total;
    result.err    = ESP_OK;
    return result;
}
