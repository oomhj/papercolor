/**
 * PaperColor — HTTP Fetcher
 *
 * Uses esp_http_client_perform() — fully handles 302 redirects.
 */
#include "news_fetcher.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cstring>
#include <cstdlib>

static const char* TAG = "Fetch";

fetch_result_t news_fetch_url(const char* url, uint32_t timeout_ms)
{
    fetch_result_t result = {nullptr, 0, ESP_FAIL};

    esp_http_client_config_t cfg = {};
    cfg.url                   = url;
    cfg.timeout_ms            = timeout_ms;
    cfg.buffer_size           = 8192;
    cfg.buffer_size_tx        = 1024;
    cfg.keep_alive_enable     = false;
    cfg.crt_bundle_attach     = esp_crt_bundle_attach;
    cfg.method                = HTTP_METHOD_GET;
    cfg.max_redirection_count = 5;       // follow redirects

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return result;

    // perform() handles entire request+redirect cycle
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "perform failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        result.err = err;
        return result;
    }

    int status = esp_http_client_get_status_code(client);
    int64_t len = esp_http_client_get_content_length(client);
    char url_buf[256] = {0};
    esp_http_client_get_url(client, url_buf, sizeof(url_buf));
    ESP_LOGI(TAG, "HTTP %d, len=%lld, url=%s", status, (long long)len, url_buf);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status);
        esp_http_client_cleanup(client);
        result.err = ESP_FAIL;
        return result;
    }

    // Read body
    size_t alloc = (len > 0) ? (size_t)len + 1 : 32768;
    char* buf = (char*)malloc(alloc);
    if (!buf) { esp_http_client_cleanup(client); return result; }

    size_t total = 0;
    int rd;
    while ((rd = esp_http_client_read(client, buf + total, alloc - total - 1)) > 0) {
        total += rd;
        if (total + 1024 >= alloc) {
            alloc *= 2;
            char* nb = (char*)realloc(buf, alloc);
            if (!nb) { free(buf); esp_http_client_cleanup(client); return result; }
            buf = nb;
        }
    }
    buf[total] = '\0';

    ESP_LOGI(TAG, "Fetched %zu bytes", total);

    esp_http_client_cleanup(client);
    result.data = buf;
    result.length = total;
    result.err = ESP_OK;
    return result;
}
