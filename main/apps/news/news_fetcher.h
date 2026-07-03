/**
 * PaperColor — RSS HTTP Fetcher
 *
 * Thin wrapper around ESP-IDF esp_http_client.
 * Downloads RSS XML into a heap buffer.
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result of an HTTP fetch. Caller must free `data`. */
typedef struct {
    char*  data;         // heap-allocated response body
    size_t length;       // bytes in data (excluding NUL)
    esp_err_t err;       // ESP_OK on success
} fetch_result_t;

/**
 * @brief HTTP GET a URL, return body in heap buffer.
 * @param url  Full URL (http:// or https://).
 * @param timeout_ms  Max milliseconds to wait.
 */
fetch_result_t news_fetch_url(const char* url, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
