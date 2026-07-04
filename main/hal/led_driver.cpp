/**
 * PaperColor — LED Driver Implementation
 *
 * SK6812/WS2812 on G21. Two LEDs, single data line.
 *
 * Async mode: dedicated FreeRTOS task reads from a queue and
 * executes LED effects without blocking the caller.
 */

#include "led_driver.h"
#include <M5Unified.hpp>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// ── Internal state ─────────────────────────────────────────────

static uint8_t s_brightness = 60;

// ── Async task ─────────────────────────────────────────────────

enum led_async_cmd {
    LED_CMD_COLOR,
    LED_CMD_BREATH,
    LED_CMD_FLASH,
    LED_CMD_OFF,
};

typedef struct {
    uint8_t cmd;
    uint8_t r, g, b;
    int     param;   // duration_ms for breath, count for flash
} led_async_msg_t;

static QueueHandle_t s_led_queue = NULL;
static TaskHandle_t  s_led_task  = NULL;
static const int     s_queue_len = 4;

static void led_task_func(void* arg)
{
    led_async_msg_t msg;
    while (1) {
        if (xQueueReceive(s_led_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.cmd) {
                case LED_CMD_COLOR:
                    led_set_color(msg.r, msg.g, msg.b);
                    break;
                case LED_CMD_BREATH:
                    led_breath(msg.r, msg.g, msg.b, msg.param);
                    break;
                case LED_CMD_FLASH:
                    led_flash(msg.r, msg.g, msg.b, msg.param);
                    break;
                case LED_CMD_OFF:
                    led_off();
                    break;
            }
        }
    }
}

void led_async_start(void)
{
    if (s_led_task) return;  // already started
    s_led_queue = xQueueCreate(s_queue_len, sizeof(led_async_msg_t));
    if (s_led_queue) {
        xTaskCreatePinnedToCore(led_task_func, "led_task", 2048,
                                NULL, 10, &s_led_task, 1);
    }
}

// ── Queue helpers ──────────────────────────────────────────────

static void send_msg(uint8_t cmd, uint8_t r, uint8_t g, uint8_t b, int param)
{
    if (!s_led_queue) return;
    led_async_msg_t msg = { cmd, r, g, b, param };

    // If queue is full, discard oldest to keep responsiveness
    if (xQueueSend(s_led_queue, &msg, 0) != pdTRUE) {
        led_async_msg_t discard;
        xQueueReceive(s_led_queue, &discard, 0);
        xQueueSend(s_led_queue, &msg, 0);
    }
}

// ── Hardware access ────────────────────────────────────────────

void led_init(void)
{
    s_brightness = 60;
    led_off();
}

void led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    M5.Led.setBrightness(s_brightness);
    M5.Led.setAllColor(r, g, b);
    M5.Led.display();
}

void led_set_single(int index, uint8_t r, uint8_t g, uint8_t b)
{
    M5.Led.setColor(index, r, g, b);
}

void led_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

void led_flush(void)
{
    M5.Led.setBrightness(s_brightness);
    M5.Led.display();
}

void led_off(void)
{
    uint8_t save = s_brightness;
    s_brightness = 0;
    led_set_color(0, 0, 0);
    s_brightness = save;
}

void led_breath(uint8_t r, uint8_t g, uint8_t b, int duration_ms)
{
    const int steps = 16;
    int half_ms = duration_ms / 2;
    for (int i = 0; i < steps; i++) {
        float phase = (float)i / (steps - 1) * 3.14159f;
        uint8_t bri = (uint8_t)(sinf(phase) * s_brightness);
        M5.Led.setBrightness(bri);
        M5.Led.setAllColor(r, g, b);
        M5.Led.display();
        vTaskDelay(pdMS_TO_TICKS(half_ms / steps));
    }
}

void led_flash(uint8_t r, uint8_t g, uint8_t b, int count)
{
    for (int i = 0; i < count; i++) {
        M5.Led.setBrightness(s_brightness);
        M5.Led.setAllColor(r, g, b);
        M5.Led.display();
        vTaskDelay(pdMS_TO_TICKS(120));
        M5.Led.setBrightness(0);
        M5.Led.display();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

// ── Async API ──────────────────────────────────────────────────

void led_async_color(uint8_t r, uint8_t g, uint8_t b)
{
    send_msg(LED_CMD_COLOR, r, g, b, 0);
}

void led_async_breath(uint8_t r, uint8_t g, uint8_t b, int duration_ms)
{
    send_msg(LED_CMD_BREATH, r, g, b, duration_ms);
}

void led_async_flash(uint8_t r, uint8_t g, uint8_t b, int count)
{
    send_msg(LED_CMD_FLASH, r, g, b, count);
}

void led_async_off(void)
{
    send_msg(LED_CMD_OFF, 0, 0, 0, 0);
}
