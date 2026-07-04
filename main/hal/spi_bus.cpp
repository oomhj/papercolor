/**
 * PaperColor — Shared SPI Bus Implementation
 *
 * Initialises the bus with all 3 pins and provides CS handover.
 * GPIO CS lines are forced HIGH during claim/release to ensure
 * no two devices are ever selected simultaneously.
 */

#include "spi_bus.h"
#include <esp_log.h>
#include <driver/spi_common.h>
#include <cstring>

static const char* TAG = "SPIBus";
static bool s_inited = false;

// ── GPIO helpers ─────────────────────────────────────────────

/** Force a CS pin HIGH (de-asserted) using GPIO output. */
static void cs_high(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
}

/** Release a CS pin so the SPI driver can control it again. */
static void cs_release(gpio_num_t pin)
{
    gpio_reset_pin(pin);
}

// ── Public API ───────────────────────────────────────────────

bool spi_bus_init(void)
{
    if (s_inited) return true;

    // Ensure all CS lines start de-asserted
    cs_high(SPI_PIN_EPD_CS);
    cs_high(SPI_PIN_SD_CS);

    // Init SPI bus with full 3-wire setup (including MISO)
    spi_bus_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mosi_io_num     = SPI_PIN_MOSI;
    cfg.miso_io_num     = SPI_PIN_MISO;    // G14 — critical for SD read
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

void spi_bus_claim_epd(void)
{
    // SD card CS → HIGH before EPD uses the bus
    cs_high(SPI_PIN_SD_CS);
    // EPD CS is managed by M5GFX, we just ensure SD is quiet
}

void spi_bus_claim_sd(void)
{
    // EPD CS → HIGH before SD uses the bus
    cs_high(SPI_PIN_EPD_CS);
    // SD CS will be managed by sdspi
}

void spi_bus_release(void)
{
    // De-assert both CS lines, then release GPIO control
    cs_high(SPI_PIN_EPD_CS);
    cs_high(SPI_PIN_SD_CS);
    // Reset pins so M5GFX / sdspi can take back control
    cs_release(SPI_PIN_EPD_CS);
    cs_release(SPI_PIN_SD_CS);
}
