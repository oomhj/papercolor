/**
 * PaperColor — Shared SPI Bus Implementation
 */

#include "spi_bus.h"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "SPIBus";
static bool s_inited = false;

bool spi_bus_init(void)
{
    if (s_inited) return true;

    spi_bus_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mosi_io_num     = SPI_PIN_MOSI;
    cfg.miso_io_num     = SPI_PIN_MISO;  // G14 — M5GFX won't set this
    cfg.sclk_io_num     = SPI_PIN_CLK;
    cfg.quadwp_io_num   = -1;
    cfg.quadhd_io_num   = -1;
    cfg.max_transfer_sz = 4092;

    esp_err_t e = spi_bus_initialize(SPI_HOST, &cfg, SPI_DMA_CH_AUTO);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "SPI2_HOST initialized (MOSI=G13, MISO=G14, CLK=G15)");
    } else if (e == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "SPI2_HOST already initialized (skipped)");
    } else {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(e));
        return false;
    }

    s_inited = true;
    return true;
}

void spi_bus_lock(void)
{
    // TODO: implement mutex/take when concurrent EPD+SD access is needed
}

void spi_bus_unlock(void)
{
    // TODO: implement mutex/give when concurrent EPD+SD access is needed
}
