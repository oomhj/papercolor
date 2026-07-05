/**
 * PaperColor — SD Card HAL Implementation
 *
 * Uses sdspi (SPI mode) on SPI2_HOST, sharing the bus with the EPD.
 * Power and card detection are handled via the M5PM1 GPIO expander.
 * SPI bus arbitration via spi_bus.h prevents conflicts with EPD.
 */

#include "sd_card.h"
#include "hal.h"
#include "spi_bus.h"
#include <cstring>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <M5Unified.hpp>
#include <M5PM1.h>
extern M5PM1* s_pmu;  // shared PMU from hal.cpp

static const char* TAG = "SDCard";

// ── Pin definitions ──────────────────────────────────────────
// Matches hardware doc: microSD shares SPI2_HOST with EPD
#define SD_CS       GPIO_NUM_47
#define SD_MOSI     GPIO_NUM_13
#define SD_MISO     GPIO_NUM_14
#define SD_CLK      GPIO_NUM_15

// M5PM1 GPIO mapping (pins configured in pc_hal_init)
#define SD_DET_PMU  M5PM1_GPIO_NUM_1   // PYG1 → CARD_DEC (card detect, active-low)

// ── State ────────────────────────────────────────────────────

static bool s_mounted = false;
static sdmmc_card_t* s_card = NULL;

// ── Card detect ──────────────────────────────────────────────
// Pins already configured in pc_hal_init() — just read.

static bool sd_detect(void)
{
    if (!s_pmu) { return false; }
    bool present = (s_pmu->digitalRead(SD_DET_PMU) == LOW);
    ESP_LOGI(TAG, "card detect: %s", present ? "present" : "absent");
    return present;
}

// ── Public API ──────────────────────────────────────────────

bool sd_card_mount(void)
{
    if (s_mounted) return true;

    sd_detect();  // log only, try mount regardless

    // SPI bus already initialized by M5GFX — just claim it for the SD mount
    if (!spi_bus_claim(SPI_OWNER_SD, pdMS_TO_TICKS(5000))) {
        ESP_LOGE(TAG, "Could not acquire SPI bus for SD mount");
        return false;
    }

    // Try mount at descending frequencies (like the demo)
    int freqs[] = {20000, 10000, 4000};
    bool ok = false;
    for (int fi = 0; fi < 3; fi++) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.max_freq_khz = freqs[fi];

        sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
        dev_cfg.host_id = SPI2_HOST;
        dev_cfg.gpio_cs = SD_CS;

        esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
            .format_if_mount_failed = false,
            .max_files = 4,
            .allocation_unit_size = 16 * 1024,
            .disk_status_check_enable = false,
            .use_one_fat = false,
        };

        esp_err_t e = esp_vfs_fat_sdspi_mount("/sd", &host, &dev_cfg, &mount_cfg, &s_card);
        if (e == ESP_OK) {
            s_mounted = true;
            ok = true;
            ESP_LOGI(TAG, "mounted @ %d KHz", freqs[fi]);
            sdmmc_card_print_info(stdout, s_card);
            break;
        }
        ESP_LOGW(TAG, "mount @ %d KHz failed: %s", freqs[fi], esp_err_to_name(e));
    }

    spi_bus_release();  // bus free for EPD after mount

    if (!ok) {
        ESP_LOGE(TAG, "SD mount failed at all frequencies");
        return false;
    }
    return true;
}

void sd_card_unmount(void)
{
    if (!s_mounted) return;

    spi_bus_claim(SPI_OWNER_SD, portMAX_DELAY);
    esp_vfs_fat_sdcard_unmount("/sd", s_card);
    s_card = NULL;
    s_mounted = false;
    spi_bus_release();

    ESP_LOGI(TAG, "unmounted");
}

bool sd_card_detect(void)
{
    return sd_detect();
}

bool sd_card_mounted(void)
{
    return s_mounted;
}

bool sd_card_lock(uint32_t timeout_ms)
{
    return spi_bus_claim(SPI_OWNER_SD, timeout_ms);
}

void sd_card_unlock(void)
{
    spi_bus_release();
}
