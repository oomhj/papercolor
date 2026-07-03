/**
 * PaperColor — Hot News Reader
 *
 * P0: Fetches RSS headlines, displays on EPD with button navigation.
 */

#include "hal/hal.h"
#include "apps/news/news_app.h"
#include <esp_log.h>

static const char* TAG = "PaperColor";

static NewsApp s_news;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PaperColor News Reader P0");

    pc_hal_init();

    // Configure display for landscape
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    // ── Init & start news app ──
    s_news.init();
    s_news.start();

    // ── Main loop ──
    while (1) {
        pc_hal_update();

        s_news.update();

        // BTN-C hold → deep sleep
        if (M5.BtnC.wasHold()) {
            ESP_LOGI(TAG, "Deep sleep");
            pc_hal_deep_sleep();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
