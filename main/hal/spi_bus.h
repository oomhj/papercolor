/**
 * PaperColor — Shared SPI Bus HAL
 *
 * SPI2_HOST is shared between EPD (M5GFX) and microSD (sdspi).
 * This module manages ownership so they don't conflict.
 *
 * Pin mapping (from docs/hardware/pin-mapping.md):
 *   CLK  = G15  (shared)
 *   MOSI = G13  (shared)
 *   MISO = G14  (SD only — EPD is write-only)
 *   EPD_CS  = G44
 *   SD_CS   = G47
 *
 * Lifecycle:
 *   spi_bus_init()     — called once from pc_hal_init() BEFORE M5.begin()
 *                         Pre-initializes the bus with all 3 pins (MOSI/MISO/CLK).
 *                         M5GFX will find the bus already exists and skip its own init.
 *   spi_bus_lock()     — claim bus for exclusive access (EPD or SD)
 *   spi_bus_unlock()   — release
 *
 * Note: The EPD and SD card must NOT be accessed simultaneously.
 *       spi_bus_lock/unlock is a cooperative mechanism — both sides must use it.
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
#define SPI_PIN_MOSI  GPIO_NUM_13
#define SPI_PIN_MISO  GPIO_NUM_14
#define SPI_PIN_CLK   GPIO_NUM_15
#define SPI_PIN_EPD_CS GPIO_NUM_44
#define SPI_PIN_SD_CS  GPIO_NUM_47

/** @brief SPI host used by both EPD and SD card */
#define SPI_HOST SPI2_HOST

/**
 * @brief  Initialize the shared SPI bus.
 *         Must be called BEFORE M5.begin() so M5GFX reuses the bus.
 * @return true on success.
 */
bool spi_bus_init(void);

/**
 * @brief  Claim exclusive access to the SPI bus.
 *         Blocks until the bus becomes available.
 * @note   Currently a no-op (both EPD and SD are used sequentially).
 *         Reserved for future concurrency management.
 */
void spi_bus_lock(void);

/**
 * @brief  Release exclusive access to the SPI bus.
 */
void spi_bus_unlock(void);

#ifdef __cplusplus
}
#endif
