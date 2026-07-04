/**
 * PaperColor — Shared SPI Bus Implementation
 *
 * Manages CS isolation between EPD (M5GFX) and SD card (sdspi).
 * Each driver owns its own CS pin. This module temporarily takes
 * over the OTHER device's CS as GPIO when claiming the bus,
 * ensuring only one device is active at a time.
 *
 * IMPORTANT: Never gpio_reset_pin() a CS pin owned by an active
 * SPI device — that destroys the device's CS configuration.
 * Always release back to input/float mode so the driver can
 * reclaim it on its next transaction.
 */

#include "spi_bus.h"
#include <esp_log.h>
#include <driver/spi_common.h>
#include <cstring>

static const char* TAG = "SPIBus";
static bool s_inited = false;

// ── Internal ─────────────────────────────────────────────────

/** Take over a CS pin as GPIO and force it HIGH. */
static void cs_force_high(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
}

/** Release a GPIO-taken-over CS pin back to input (SPI driver reclaims). */
static void cs_release_gpio(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

// ── Public API ───────────────────────────────────────────────

bool spi_bus_init(void)
{
    if (s_inited) return true;

    // At boot, no driver has claimed CS pins yet — safe to init both HIGH
    cs_force_high(SPI_PIN_EPD_CS);
    cs_force_high(SPI_PIN_SD_CS);
    cs_release_gpio(SPI_PIN_EPD_CS);
    cs_release_gpio(SPI_PIN_SD_CS);

    // Init SPI bus with full 3-wire setup (including MISO for SD)
    spi_bus_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mosi_io_num     = SPI_PIN_MOSI;
    cfg.miso_io_num     = SPI_PIN_MISO;
    cfg.sclk_io_num     = SPI_PIN_CLK;
    cfg.quadwp_io_num   = -1;
    cfg.quadhd_io_num   = -1;
    cfg.max_transfer_sz = 4092;

    esp_err_t e = spi_bus_initialize(SPI_HOST, &cfg, SPI_DMA_CH_AUTO);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "SPI2_HOST inited (MOSI=G13 MISO=G14 CLK=G15)");
    } else if (e == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "SPI2_HOST already init (skipped)");
    } else {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(e));
        return false;
    }

    s_inited = true;
    return true;
}

/**
 * Called before EPD (M5GFX) uses the bus.
 * Takes over SD_CS as GPIO and forces it HIGH so SD stays quiet.
 * EPD_CS is owned by M5GFX — we leave it alone.
 */
void spi_bus_claim_epd(void)
{
    cs_force_high(SPI_PIN_SD_CS);
}

/**
 * Called before SD card (sdspi) uses the bus.
 * Takes over EPD_CS as GPIO and forces it HIGH so EPD stays quiet.
 * SD_CS is owned by sdspi — we leave it alone.
 */
void spi_bus_claim_sd(void)
{
    cs_force_high(SPI_PIN_EPD_CS);
}

/**
 * Release the GPIO-taken-over CS pin so the original SPI driver
 * can reclaim it on its next transaction.
 * Which pin to release depends on who last claimed the bus.
 */
void spi_bus_release(void)
{
    // Release both — whichever was taken over will go back to input.
    // For the one that wasn't taken over, this is a no-op
    // (it's already in its SPI-driver-controlled state).
    cs_release_gpio(SPI_PIN_EPD_CS);
    cs_release_gpio(SPI_PIN_SD_CS);
}
