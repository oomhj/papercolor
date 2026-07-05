/**
 * PaperColor — Shared SPI Bus Arbiter Implementation
 *
 * Mutex-based ownership arbitration between EPD and SD card on SPI2_HOST.
 * The mutex ensures only one owner accesses the shared bus at a time.
 * CS isolation is handled by the SPI drivers themselves — each manages
 * its own chip select pin. Forcing CS pins as GPIO (as done previously)
 * actually BREAKS the SPI peripheral's ability to drive those pins.
 *
 * Correctness relies on:
 *   1. FreeRTOS Mutex — mutual exclusion between EPD and SD access
 *   2. Separate CS lines (EPD_CS=G44, SD_CS=G47) — no bus conflict
 *   3. M5GFX sets cfg.bus_shared=true — de-asserts CS after each transaction
 */

#include "spi_bus.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>

static const char* TAG = "SPIBus";

// ── State ───────────────────────────────────────────────────────

static SemaphoreHandle_t s_mutex = NULL;
static spi_owner_t s_owner = SPI_OWNER_NONE;
static bool s_inited = false;

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
    ESP_LOGI(TAG, "Arbiter initialized (mutex only — no CS pin manipulation)");
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

    s_owner = owner;
    ESP_LOGD(TAG, "Claimed by %s", owner == SPI_OWNER_EPD ? "EPD" : "SD");
    return true;
}

void spi_bus_release(void)
{
    if (s_owner == SPI_OWNER_NONE) return;

    spi_owner_t old = s_owner;
    s_owner = SPI_OWNER_NONE;
    xSemaphoreGive(s_mutex);
    ESP_LOGD(TAG, "Released by %s", old == SPI_OWNER_EPD ? "EPD" : "SD");
}

spi_owner_t spi_bus_get_owner(void)
{
    return s_owner;
}
