/**
 * PaperColor — Shared SPI Bus HAL
 *
 * Manages SPI2_HOST shared between EPD (M5GFX) and microSD (sdspi).
 * Responsibilities:
 *   - Bus initialization with all 3 pins (including MISO)
 *   - CS isolation — ensure only one device is active at a time
 *   - Clock source stability
 *
 * Pin mapping (docs/hardware/pin-mapping.md):
 *   CLK    = G15   (shared)
 *   MOSI   = G13   (shared)
 *   MISO   = G14   (SD only – EPD is write-only, but bus needs it)
 *   EPD_CS = G44
 *   SD_CS  = G47
 *
 * Lifecycle:
 *   spi_bus_init()       — once from pc_hal_init(), BEFORE M5.begin()
 *   spi_bus_claim_epd()  — before EPD transaction
 *   spi_bus_release()    — after EPD or SD transaction
 *   spi_bus_claim_sd()   — before SD transaction
 *
 * The claim/release pattern ensures all CS lines are properly
 * de-asserted during handover, preventing cross-device interference.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Pin definitions ──────────────────────────────────────────

#define SPI_PIN_MOSI   GPIO_NUM_13
#define SPI_PIN_MISO   GPIO_NUM_14
#define SPI_PIN_CLK    GPIO_NUM_15
#define SPI_PIN_EPD_CS GPIO_NUM_44
#define SPI_PIN_SD_CS  GPIO_NUM_47

#define SPI_HOST       SPI2_HOST

// ── API ──────────────────────────────────────────────────────

/**
 * @brief  Initialize the shared SPI bus.
 *         Must be called BEFORE M5.begin() so M5GFX reuses it.
 *         Sets up MOSI/MISO/CLK on SPI2_HOST.
 */
bool spi_bus_init(void);

/**
 * @brief  Claim bus for EPD access.
 *         Forces SD_CS HIGH, then EPD can assert its own CS.
 */
void spi_bus_claim_epd(void);

/**
 * @brief  Claim bus for SD card access.
 *         Forces EPD_CS HIGH, then SD csdspi can assert SD_CS.
 */
void spi_bus_claim_sd(void);

/**
 * @brief  Release bus after any transaction.
 *         De-asserts all CS lines to idle-high state.
 */
void spi_bus_release(void);

#ifdef __cplusplus
}
#endif
