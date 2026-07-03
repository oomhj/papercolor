/**
 * PaperColor — Network Photo Album
 */
#include "hal/hal.h"
#include "apps/album/album_app.h"
#include <esp_log.h>

static const char* TAG = "PaperColor";
static AlbumApp s_album;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PaperColor Album");
    pc_hal_init();

    s_album.init();
    s_album.start();

    while (1) {
        pc_hal_update();
        s_album.update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
