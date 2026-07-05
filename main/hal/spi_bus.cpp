/**
 * PaperColor — Shared SPI Bus Arbiter Implementation
 *
 * Mutex-based ownership arbitration between EPD and SD card on SPI2_HOST.
 * CS isolation ensures the inactive device's chip select is de-asserted.
 */

#include "spi_bus.h"
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char* TAG = "SPIBus";

// ── State ───────────────────────────────────────────────────────

static SemaphoreHandle_t s_mutex = NULL;
static spi_owner_t s_owner = SPI_OWNER_NONE;
static bool s_inited = false;

// ── CS isolation helpers ────────────────────────────────────────
// Force a CS pin HIGH via GPIO to ensure the other device is deselected.

static void cs_force_high(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
}

static void cs_release(gpio_num_t pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

// ── Public API ──────────────────────────────────────────────────

bool spi_bus_init(void)
{
    if (s_inited) return true;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create arbitration mutex");
        return false;
    }

    s_owner = SPI_OWNER_NONE;
    s_inited = true;
    ESP_LOGI(TAG, "Arbiter initialized (mutex only — M5GFX owns physical SPI2_HOST)");
    return true;
}

bool spi_bus_claim(spi_owner_t owner, uint32_t timeout_ms)
{
    if (!s_inited || owner == SPI_OWNER_NONE) return false;
    if (owner == s_owner) return true;  // recursive claim OK (same owner)

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY
                                                   : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_mutex, ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Claim timeout (owner=%d, timeout=%u ms)", (int)owner, timeout_ms);
        return false;
    }

    // CS isolation: force the OTHER device's CS HIGH
    gpio_num_t other_cs = (owner == SPI_OWNER_EPD) ? SPI_PIN_SD_CS : SPI_PIN_EPD_CS;
    cs_force_high(other_cs);
    esp_rom_delay_us(10);  // barrier — let CS settle before first SPI clock

    s_owner = owner;
    ESP_LOGD(TAG, "Claimed by %s", owner == SPI_OWNER_EPD ? "EPD" : "SD");
    return true;
}

void spi_bus_release(void)
{
    if (s_owner == SPI_OWNER_NONE) return;

    // Release the taken-over CS pin back to input (SPI driver reclaims it)
    gpio_num_t other_cs = (s_owner == SPI_OWNER_EPD) ? SPI_PIN_SD_CS : SPI_PIN_EPD_CS;
    cs_release(other_cs);

    spi_owner_t old = s_owner;
    s_owner = SPI_OWNER_NONE;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "Released by %s", old == SPI_OWNER_EPD ? "EPD" : "SD");
}

spi_owner_t spi_bus_get_owner(void)
{
    return s_owner;
}
