/**
 * PaperColor — SD Card HAL Implementation
 *
 * Uses sdspi (SPI mode) on SPI2_HOST, sharing the bus with the EPD.
 * Power switching and card detection are handled via the M5PM1 GPIO expander.
 */

#include "sd_card.h"
#include "hal.h"
#include <cstring>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <M5Unified.hpp>
#include <M5PM1.h>

static const char* TAG = "SDCard";

// ── Pin definitions ──────────────────────────────────────────
// Matches hardware doc: microSD shares SPI2_HOST with EPD
#define SD_CS       GPIO_NUM_47
#define SD_MOSI     GPIO_NUM_13
#define SD_MISO     GPIO_NUM_14
#define SD_CLK      GPIO_NUM_15

// M5PM1 GPIO mapping
//   PYG1 → CARD_DEC (card detect, input)
//   PYG3 → PY_SD_PWR_EN (SD power, output)
#define SD_PWR_PMU  M5PM1_GPIO_NUM_3
#define SD_DET_PMU  M5PM1_GPIO_NUM_1

// ── State ────────────────────────────────────────────────────

static bool s_mounted = false;
static sdmmc_card_t* s_card = NULL;

// ── Power control (M5PM1) ───────────────────────────────────

static M5PM1& get_pmu()
{
    static M5PM1 pmu;
    static bool inited = false;
    if (!inited) {
        pmu.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
        inited = true;
    }
    return pmu;
}

static void sd_power(bool on)
{
    auto& pmu = get_pmu();
    pmu.pinMode(SD_PWR_PMU, M5PM1_GPIO_MODE_OUTPUT);
    pmu.digitalWrite(SD_PWR_PMU, on ? HIGH : LOW);
    vTaskDelay(pdMS_TO_TICKS(50));  // let power stabilize
}

static bool sd_detect(void)
{
    auto& pmu = get_pmu();
    pmu.pinMode(SD_DET_PMU, M5PM1_GPIO_MODE_INPUT);
    // Card detect is active-low (LOW = card present)
    bool present = (pmu.digitalRead(SD_DET_PMU) == LOW);
    ESP_LOGD(TAG, "card detect: %s", present ? "present" : "absent");
    return present;
}

// ── Public API ───────────────────────────────────────────────

bool sd_card_mount(void)
{
    if (s_mounted) return true;

    if (!sd_detect()) {
        ESP_LOGW(TAG, "no card detected");
        return false;
    }

    // Power on
    sd_power(true);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Ensure SPI2_HOST bus is initialized (may already be from M5GFX)
    spi_bus_config_t bus_cfg;
    memset(&bus_cfg, 0, sizeof(bus_cfg));
    bus_cfg.mosi_io_num     = SD_MOSI;
    bus_cfg.miso_io_num     = SD_MISO;
    bus_cfg.sclk_io_num     = SD_CLK;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 4092;
    esp_err_t bus_e = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (bus_e != ESP_OK && bus_e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_init failed: %s", esp_err_to_name(bus_e));
        sd_power(false);
        return false;
    }
    if (bus_e == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "SPI2_HOST already initialized (by M5GFX)");
    }

    // Configure sdspi device
    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.host_id   = SPI2_HOST;
    dev_cfg.gpio_cs   = SD_CS;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 8000;  // 8MHz — conservative for shared bus

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat            = false,
    };

    esp_err_t e = esp_vfs_fat_sdspi_mount("/sd", &host, &dev_cfg, &mount_cfg, &s_card);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(e));
        sd_power(false);
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "mounted, size=%lluMB, speed=%uMHz",
             (unsigned long long)(s_card->csd.capacity * s_card->csd.sector_size) / (1024 * 1024),
             host.max_freq_khz / 1000);

    // Determine card type from CSD structure
    const char* type = (s_card->csd.capacity == 0) ? "SDSC" : "SDHC/SDXC";
    ESP_LOGI(TAG, "card type: %s", type);

    return true;
}

void sd_card_unmount(void)
{
    if (!s_mounted) return;

    // Flush and unmount
    esp_vfs_fat_sdcard_unmount("/sd", s_card);
    s_card = NULL;
    s_mounted = false;

    // Power off
    sd_power(false);
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
