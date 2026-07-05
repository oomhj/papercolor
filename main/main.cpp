/**
 * PaperColor — Network Photo Album
 *
 * Main entry: runs the Album app with the standard lifecycle pattern.
 * To switch apps, replace AlbumApp with another app class.
 */
#include "hal/hal.h"
#include "apps/album/album_app.h"
#include <esp_log.h>

static const char* TAG = "PaperColor";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PaperColor starting...");

    // ── HAL init ──
    pc_hal_init();

    // ── App lifecycle ──
    AlbumApp app;
    app.init();
    app.start();

    // ── Main loop ──
    while (true) {
        pc_hal_update();
        app.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // (unreachable)
    // app.stop();
    // app.deinit();
}
