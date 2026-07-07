/**
 * PaperColor — Simple Key=Value Config File Helpers
 *
 * Read/write key=value pairs in a text file on FatFS/SD.
 * Shared between album_app and wifi_provisioning.
 */

#pragma once

#include <cstddef>
#include <cstdbool>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a value for @p key from a key=value file.
 * @return true if found.
 */
bool config_read_val(const char* path, const char* key, char* val, size_t val_sz);

/**
 * @brief Write a key=value pair to a file.
 *        Updates existing line or appends. Atomic via tmp+rename.
 */
void config_write_val(const char* path, const char* key, const char* val);

#ifdef __cplusplus
}
#endif
