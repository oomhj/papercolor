/**
 * PaperColor — M5Stack PaperColor Demo
 *
 * Board:  M5Stack PaperColor (ESP32-S3R8)
 * Display: EL040EF1 (ED2208-DOA) — 4" E6 Full-Color E-Paper, 400x600
 * Framework: Arduino (PlatformIO)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <M5PM1.h>
#include "config.h"

// ============================================================
// Global Objects
// ============================================================
M5GFX &display = M5.Display;
M5Canvas canvas(&display);
M5PM1 pmu;

// ============================================================
// Forward Declarations
// ============================================================
void setupDisplay();
void setupPMU();
void setupAudioPower();
void drawDemoUI();

// ============================================================
// Setup
// ============================================================
void setup() {
    auto cfg = M5.config();
    cfg.external_display  = false;   // EPD is internal
    cfg.external_speaker  = false;   // ES8311 is internal
    M5.begin(cfg);

    Serial.println("\n========================================");
    Serial.println("  PaperColor v" APP_VERSION);
    Serial.println("========================================");

    // Enable audio codec + mic power rail
    setupAudioPower();

    // Initialize Power Management
    setupPMU();

    // Initialize E-Paper Display
    setupDisplay();

    // Show demo UI
    drawDemoUI();

    Serial.println("Setup complete.");
}

// ============================================================
// Loop
// ============================================================
void loop() {
    M5.update();

    // Button A (G10) — Refresh display
    if (M5.BtnA.wasPressed()) {
        Serial.println("BTN-A: Refresh display");
        drawDemoUI();
    }

    // Button B (G9) — Power info
    if (M5.BtnB.wasPressed()) {
        Serial.println("BTN-B: Power info");
        float v = pmu.getBatteryVoltage();
        float pct = pmu.getBatteryLevel();
        bool chg = pmu.isCharging();
        Serial.printf("  Battery: %.2fV (%.0f%%) %s\n",
                      v, pct, chg ? "[CHARGING]" : "");

        // Refresh display with updated battery info
        drawDemoUI();
    }

    // Button C (G1) — Deep sleep
    if (M5.BtnC.wasPressed()) {
        Serial.println("BTN-C: Enter deep sleep");
        display.sleep();
        delay(100);
        M5.Power.deepSleep();
    }

    delay(50);
}

// ============================================================
// Audio Power Rail
// ============================================================
void setupAudioPower() {
    pinMode(PIN_AUDIO_PWR_EN, OUTPUT);
    digitalWrite(PIN_AUDIO_PWR_EN, HIGH);   // Enable ES8311 + ES7210
    delay(10);

    pinMode(PIN_SPK_EN, OUTPUT);
    digitalWrite(PIN_SPK_EN, LOW);           // Speaker amp off by default
}

// ============================================================
// PMU — M5PM1 Power Management (addr 0x6e)
// ============================================================
void setupPMU() {
    if (!pmu.begin()) {
        Serial.println("WARN: M5PM1 init failed");
        return;
    }
    Serial.println("M5PM1 PMU initialized");

    // Enable EPD power (PYG0 / PY_EPD_EN)
    pmu.setEPDPower(true);
    pmu.setEPDVoltage(EPD_VOLTAGE);

    // Enable audio codec power
    pmu.setAudioPower(true);

    // Report battery
    float v = pmu.getBatteryVoltage();
    float pct = pmu.getBatteryLevel();
    bool chg = pmu.isCharging();
    Serial.printf("  Battery: %.2fV (%.0f%%) %s\n",
                  v, pct, chg ? "CHARGING" : "");

    // Report power rail status
    Serial.printf("  EPD PWR: %s\n", pmu.getEPDPower() ? "ON" : "OFF");
    Serial.printf("  Audio PWR: %s\n", pmu.getAudioPower() ? "ON" : "OFF");
}

// ============================================================
// E-Paper Display — EL040EF1 (ED2208-DOA)
// ============================================================
void setupDisplay() {
    display.begin();
    display.setRotation(EPD_ROTATION);
    display.setColorDepth(EPD_COLOR_DEPTH);
    display.setEpdMode(epd_mode_t::epd_quality);

    // Off-screen framebuffer
    canvas.setColorDepth(EPD_COLOR_DEPTH);
    canvas.createSprite(display.width(), display.height());

    Serial.printf("Display: %d x %d (%d-bit)\n",
                  display.width(), display.height(),
                  display.getColorDepth());
}

// ============================================================
// Demo User Interface
// ============================================================
void drawDemoUI() {
    const int w = display.width();   // 400
    const int h = display.height();  // 600

    // Clear
    canvas.fillScreen(TFT_WHITE);

    // ---- Header ----
    canvas.fillRect(0, 0, w, 48, TFT_DARKBLUE);
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextSize(2);
    canvas.drawString("PaperColor", 16, 14);

    // ---- Board Info ----
    canvas.setTextColor(TFT_BLACK);
    canvas.setTextSize(1);
    int y = 72;

    const char *info[] = {
        "Board:  M5Stack PaperColor",
        "SoC:    ESP32-S3R8 (240MHz)",
        "Flash:  16MB  |  PSRAM: 8MB",
        "Display: EL040EF1 (ED2208-DOA)",
        "Panel:  4\" E6 Full-Color E-Paper",
        "Res:    400 x 600",
        "",
    };
    for (auto line : info) {
        canvas.drawString(line, 20, y);
        y += 22;
    }

    // ---- Battery Section ----
    y += 10;
    canvas.setTextColor(TFT_DARKGREEN);
    canvas.setTextSize(1);
    float battPct = pmu.getBatteryLevel();
    bool charging = pmu.isCharging();
    float battV = pmu.getBatteryVoltage();

    canvas.drawString("--- Power ---", 20, y); y += 22;
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("Battery: " + String(battPct, 0) + "%  " +
                      String(battV, 2) + "V" +
                      (charging ? "  [CHARGING]" : ""), 20, y);

    // Battery bar
    y += 30;
    const int barW = 200, barH = 16;
    canvas.drawRect(20, y, barW, barH, TFT_DARKGREEN);
    int fillW = (int)((barW - 2) * constrain(battPct, 0, 100) / 100.0);
    uint16_t barColor = (battPct > 20) ? TFT_DARKGREEN : TFT_RED;
    if (fillW > 0) canvas.fillRect(21, y + 1, fillW, barH - 2, barColor);

    // ---- Pin Map Summary ----
    y += 40;
    canvas.setTextColor(TFT_DARKGRAY);
    canvas.setTextSize(1);
    canvas.drawString("--- Key Pins ---", 20, y); y += 20;
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString("BTN-A:G10  BTN-B:G9  BTN-C:G1", 20, y); y += 18;
    canvas.drawString("RGB:G21  IR:G48  SD_CS:G47", 20, y); y += 18;
    canvas.drawString("EPD:CS:G44  DC:G43  BUSY:G11  RST:G12", 20, y);

    // ---- Button Hints ----
    canvas.setTextColor(TFT_DARKGRAY);
    canvas.drawString("[BTN-A] Refresh  [BTN-B] Info  [BTN-C] Sleep",
                      20, h - 36);

    // Push to EPD
    canvas.pushSprite(0, 0);
    display.display();
}
