/**
 * PaperColor — Template App Implementation
 *
 * Shows a minimal application lifecycle.
 * Use as starting point for new PaperColor apps.
 */
#include "template_app.h"
#include "hal/hal.h"
#include <cstdio>
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "TemplateApp";

bool TemplateApp::init()
{
    ESP_LOGI(TAG, "init");
    return true;
}

void TemplateApp::deinit()
{
    ESP_LOGI(TAG, "deinit");
    _running = false;
}

void TemplateApp::start()
{
    ESP_LOGI(TAG, "start");
    _running      = true;
    _last_tick_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Render initial screen
    render();
}

void TemplateApp::stop()
{
    ESP_LOGI(TAG, "stop");
    _running = false;
}

void TemplateApp::update()
{
    if (!_running) return;

    handle_buttons();

    // Optional: periodic refresh (e.g., once per second)
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (now - _last_tick_ms >= 1000) {
        _last_tick_ms = now;
        // Update on-screen time, sensor values, etc.
    }
}

void TemplateApp::handle_buttons()
{
    if (BTN_DOWN.wasPressed()) {
        ESP_LOGI(TAG, "BTN_DOWN pressed");
        render();
    }
    if (BTN_UP.wasPressed()) {
        ESP_LOGI(TAG, "BTN_UP pressed");
    }
    if (BTN_TOP.wasPressed()) {
        ESP_LOGI(TAG, "BTN_TOP pressed — deep sleep");
        pc_hal_deep_sleep();
    }
}

void TemplateApp::render()
{
    const int w = g_canvas->width();
    const int h = g_canvas->height();

    g_canvas->fillScreen(TFT_WHITE);

    // Placeholder
    g_canvas->fillRect(0, 0, w, 48, 0x2118);
    g_canvas->setTextColor(TFT_WHITE);
    g_canvas->setFont(&fonts::Font4);
    g_canvas->drawString("Template App", 16, 12);

    g_canvas->setFont(&fonts::Font2);
    g_canvas->setTextColor(TFT_BLACK);
    g_canvas->drawString("Add your UI here", 16, 100);

    // Button hints
    g_canvas->setFont(&fonts::Font0);
    g_canvas->setTextColor(TFT_DARKGRAY);
    g_canvas->drawString("[BTN-A] Refresh  [BTN-B] Action  [BTN-C] Sleep",
                         16, h - 30);

    hal_display();
}
