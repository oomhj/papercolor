/**
 * PaperColor — SPI Bus Arbiter Implementation
 *
 * Owner token state machine:
 *   IDLE ──acquire(EPD)──→ EPD_OWNS ──release──→ IDLE
 *   IDLE ──acquire(SD)───→ SD_OWNS  ──release──→ IDLE
 *
 * Each driver (M5GFX, sdspi) manages its own CS pin.
 * The arbiter only ensures isolation when switching owners.
 */

#include "spi_bus.h"
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <driver/spi_common.h>
#include <cstring>

static const char* TAG = "SPIBus";
static bool     s_inited = false;
static spi_owner_t s_owner = SPI_OWNER_NONE;
static gpio_num_t  s_taken_over = GPIO_NUM_NC;  // CS pin taken over as GPIO

// ── GPIO helpers ─────────────────────────────────────────────

static void cs_force_high(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
}

static void cs_release_pin(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

// ── Public API ───────────────────────────────────────────────

bool spi_bus_init(void)
{
    if (s_inited) return true;

    // Ensure both CS start de-asserted
    cs_force_high(SPI_PIN_EPD_CS);
    cs_force_high(SPI_PIN_SD_CS);
    cs_release_pin(SPI_PIN_EPD_CS);
    cs_release_pin(SPI_PIN_SD_CS);

    // Init SPI bus with full 3-wire (includes MISO for SD reads)
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

void spi_bus_acquire(spi_owner_t owner)
{
    if (!s_inited || owner == SPI_OWNER_NONE) return;
    if (owner == s_owner) return;  // already ours

    // Release previous owner if any
    if (s_owner != SPI_OWNER_NONE) spi_bus_release();

    // Take over the OTHER device's CS as GPIO, force HIGH
    gpio_num_t other_cs = (owner == SPI_OWNER_EPD) ? SPI_PIN_SD_CS : SPI_PIN_EPD_CS;
    cs_force_high(other_cs);
    s_taken_over = other_cs;
    s_owner = owner;

    // Barrier delay: let CS settle before next transaction
    esp_rom_delay_us(10);
}

void spi_bus_release(void)
{
    if (s_owner == SPI_OWNER_NONE) return;

    // Release taken-over CS pin back to input (SPI driver reclaims)
    if (s_taken_over != GPIO_NUM_NC) {
        cs_release_pin(s_taken_over);
        s_taken_over = GPIO_NUM_NC;
    }

    s_owner = SPI_OWNER_NONE;
}
