/**
 * PaperColor — Image Downloader
 *
 * HTTP image download + SD card persistence.
 * Used by SlideShow to fetch daily images.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>

/** Maximum JPEG file size we'll accept (2 MB). */
#define DL_MAX_JPEG_BYTES (2 * 1024 * 1024)

/** Default image source URL. */
#define DL_DEFAULT_URL "https://bing.img.run/rand_1366x768.php"

/**
 * @brief Fetch an image from a URL into a malloc'd buffer.
 *        Caller must free(*out) when done.
 * @return true on success.
 */
bool dl_fetch(const char* url, uint8_t** out, size_t* out_len);

/**
 * @brief Fetch image from default URL.
 *        Convenience wrapper around dl_fetch(DL_DEFAULT_URL, ...).
 */
bool dl_fetch_default(uint8_t** out, size_t* out_len);

/**
 * @brief Save a JPEG buffer to a numbered file under @p album_dir.
 *        Writes atomically via .tmp + rename.
 * @param album_dir  e.g. "/sd/album"
 * @param index      1..N, file becomes "%s/%d.jpg"
 */
bool dl_save(const char* album_dir, int index, const uint8_t* jpeg, size_t len);

/**
 * @brief Get the server time from the last HTTP download.
 *        Returns "YYYY-MM-DDTHH:MM:SSZ" (UTC), or NULL.
 *        Valid until the next dl_fetch() call.
 */
const char* dl_last_date(void);
