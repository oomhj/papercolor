/**
 * PaperColor — SD Card HAL
 *
 * FatFS + SPI mode, shares SPI bus with EPD (SPI2_HOST).
 * Power and card detect controlled via M5PM1 GPIO expander.
 *
 * Usage:
 *   sd_card_mount();                    // power on + mount
 *   if (sd_card_mounted()) {
 *       FILE* f = fopen("/sd/test.txt", "w");
 *       fprintf(f, "hello");
 *       fclose(f);
 *   }
 *   sd_card_unmount();                  // sync + power off
 *
 * Notes:
 *   - EPD + SD share SPI2_HOST (CLK=G15, MOSI=G13)
 *   - M5GFX manages the SPI bus internally; sdspi hooks into the same host
 *   - M5PM1 controls SD power (PYG3) via I2C — do not call while M5PM1 is uninitialized
 *   - Card detect uses M5PM1 PYG1 (CARD_DEC)
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <sys/unistd.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Mount SD card with FatFS.
 *         Powers on the SD slot via M5PM1, initializes SPI, mounts filesystem.
 * @return true on success.
 * @note   Must call AFTER M5.begin() and M5PM1.begin().
 */
bool sd_card_mount(void);

/**
 * @brief  Unmount SD card and power off.
 *         Flushes all cached writes before shutting down.
 */
void sd_card_unmount(void);

/**
 * @brief  Check if SD card is physically inserted.
 * @return true if card present.
 * @note   Uses M5PM1 GPIO — must be called after M5PM1 init.
 */
bool sd_card_detect(void);

/**
 * @brief  Check if filesystem is currently mounted.
 */
bool sd_card_mounted(void);

#ifdef __cplusplus
}
#endif
