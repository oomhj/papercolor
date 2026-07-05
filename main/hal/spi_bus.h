/**
 * PaperColor — Shared SPI Bus Arbiter
 *
 * SPI2_HOST is shared between EPD (M5GFX) and microSD (sdspi).
 * Each driver manages its own CS pin. This module provides:
 *   - Bus initialization with all 3 pins (including MISO for SD)
 *   - Owner token arbitration (only one owner at a time)
 *   - CS isolation barrier (force other CS HIGH + delay)
 *
 * Pin mapping:
 *   CLK=G15, MOSI=G13, MISO=G14, EPD_CS=G44, SD_CS=G47
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI_PIN_MOSI   GPIO_NUM_13
#define SPI_PIN_MISO   GPIO_NUM_14
#define SPI_PIN_CLK    GPIO_NUM_15
#define SPI_PIN_EPD_CS GPIO_NUM_44
#define SPI_PIN_SD_CS  GPIO_NUM_47
#define SPI_HOST        SPI2_HOST

typedef enum {
    SPI_OWNER_NONE = 0,
    SPI_OWNER_EPD  = 1,
    SPI_OWNER_SD   = 2,
} spi_owner_t;

/**
 * @brief  Initialize the shared SPI bus. Must be called BEFORE M5.begin().
 */
bool spi_bus_init(void);

/**
 * @brief  Acquire the bus for a specific owner.
 *         Forces the other owner's CS HIGH as a safety net,
 *         then adds a 10µs barrier delay.
 *         If bus is held by a different owner, releases it first.
 */
void spi_bus_acquire(spi_owner_t owner);

/**
 * @brief  Release bus ownership.
 *         Releases the GPIO-taken-over CS pin back to input mode.
 */
void spi_bus_release(void);

#ifdef __cplusplus
}
#endif
