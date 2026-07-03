/**
 * PaperColor — Unified Button Definitions
 *
 * Logical button names mapped to physical GPIO pins.
 * Use BTN_TOP / BTN_UP / BTN_DOWN instead of M5.BtnC/BtnB/BtnA
 * throughout the project for clarity.
 *
 * Mapping:
 *   BTN_TOP   = G1   (was M5.BtnC)
 *   BTN_UP    = G9   (was M5.BtnB)
 *   BTN_DOWN  = G10  (was M5.BtnA)
 */
#pragma once

#include <M5Unified.hpp>

#define BTN_TOP   M5.BtnC   // G1  — top action (refresh / confirm)
#define BTN_UP    M5.BtnB   // G9  — scroll up / previous
#define BTN_DOWN  M5.BtnA   // G10 — scroll down / next
