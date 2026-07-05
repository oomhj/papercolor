/**
 * PaperColor — Shared SPI Bus HAL
 *
 * SPI2_HOST is shared between EPD (M5GFX) and microSD (sdspi).
 * This module provides mutex-based ownership arbitration so they don't conflict.
 *
 * Pin mapping (from docs/hardware/pin-mapping.md):
 *   CLK  = G15  (shared)
 *   MOSI = G13  (shared)
 *   MISO = G14  (SD only — EPD is write-only)
 *   EPD_CS  = G44
 *   SD_CS   = G47
 *
 * Lifecycle:
 *   spi_bus_init()     — called once from pc_hal_init() AFTER M5.begin()
 *                         Creates the arbitration mutex only (M5GFX owns physical bus init).
 *   spi_bus_claim()    — take mutex + CS isolation
 *   spi_bus_release()  — release mutex
 *
 * Note: M5GFX sets cfg.bus_shared = true for the EPD panel (M5GFX.cpp:1904),
 *       meaning it de-asserts CS after each transaction — cooperative arbitration
 *       via this module is safe.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <driver/gpio.h>
#include <driver/spi_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPIO pin numbers for the shared SPI bus */
#define SPI_PIN_MOSI   GPIO_NUM_13
#define SPI_PIN_MISO   GPIO_NUM_14
#define SPI_PIN_CLK    GPIO_NUM_15
#define SPI_PIN_EPD_CS GPIO_NUM_44
#define SPI_PIN_SD_CS  GPIO_NUM_47

/** @brief SPI host used by both EPD and SD card */
#define SPI_HOST SPI2_HOST

/** @brief Bus owner tokens for arbitration */
typedef enum {
    SPI_OWNER_NONE = 0,
    SPI_OWNER_EPD,    // E-ink display
    SPI_OWNER_SD,     // microSD card
} spi_owner_t;

/**
 * @brief  Initialize the SPI bus arbiter.
 *         Creates the arbitration mutex. Does NOT call spi_bus_initialize()
 *         — M5GFX handles physical bus init during M5.begin().
 *         Safe to call multiple times.
 * @return true on success.
 */
bool spi_bus_init(void);

/**
 * @brief  Claim exclusive access to the SPI bus for an owner.
 *         Blocks up to timeout_ms if another owner holds the bus.
 *         CS isolation is handled by the SPI drivers — no pins are touched.
 * @param  owner      SPI_OWNER_EPD or SPI_OWNER_SD.
 * @param  timeout_ms Max wait in ms (UINT32_MAX = wait forever).
 * @return true if claim succeeded.
 */
bool spi_bus_claim(spi_owner_t owner, uint32_t timeout_ms);

/**
 * @brief  Release exclusive access to the SPI bus.
 */
void spi_bus_release(void);

/**
 * @brief  Get current bus owner.
 * @return SPI_OWNER_NONE if bus is free.
 */
spi_owner_t spi_bus_get_owner(void);

#ifdef __cplusplus
}
#endif
