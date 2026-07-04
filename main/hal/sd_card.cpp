/**
 * PaperColor — SD Card HAL Implementation
 *
 * Uses sdspi (SPI mode) on SPI2_HOST, sharing the bus with the EPD.
 * Power switching and card detection are handled via the M5PM1 GPIO expander.
 */

#include "sd_card.h"
#include "hal.h"
#include "spi_bus.h"
#include <cstring>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <M5PM1.h>

static const char* TAG = "SDCard";

// Pin SD_CS from shared SPI bus HAL. MOSI/MISO/CLK are on SPI_HOST.
#define SD_CS       SPI_PIN_SD_CS

// M5PM1 GPIO mapping (see docs/hardware/pin-mapping.md)
//   PYG1 → CARD_DEC  (card detect, input, active low)
//   PYG3 → PY_SD_PWR_EN  (SD power, output)
//   PYG4 → PY_SD_DET_EN  (SD detect pull-up enable, must be HIGH)
#define SD_PWR_PMU     M5PM1_GPIO_NUM_3
#define SD_DET_PMU     M5PM1_GPIO_NUM_1
#define SD_DET_EN_PMU  M5PM1_GPIO_NUM_4

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
    // Enable detect circuit: PYG4 = PY_SD_DET_EN
    pmu.pinMode(SD_DET_EN_PMU, M5PM1_GPIO_MODE_OUTPUT);
    pmu.digitalWrite(SD_DET_EN_PMU, HIGH);
    vTaskDelay(pdMS_TO_TICKS(5));
    // Read card detect: PYG1 = CARD_DEC, active-low
    pmu.pinMode(SD_DET_PMU, M5PM1_GPIO_MODE_INPUT);
    bool present = (pmu.digitalRead(SD_DET_PMU) == LOW);
    ESP_LOGI(TAG, "card detect: %s", present ? "present" : "absent");
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

    // SD card uses SPI2_HOST which is shared with the EPD.
    // The bus is already initialized by M5GFX (MOSI+CLK).
    // sdspi internally configures MISO via GPIO matrix.
    // We do NOT free/reinit the bus here — that would break the EPD.

    // Configure sdspi device
    sdspi_device_config_t dev_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_cfg.host_id   = SPI_HOST;
    dev_cfg.gpio_cs   = SD_CS;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI_HOST;
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

    // Power off (SPI bus remains for EPD — M5GFX owns it)
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
