// M5Stack PaperDIY (K159 / K159-B) user demo, ported from the ESP-IDF project.
//
//   - battery voltage (updates every 10 s)
//   - Wi-Fi scan (button A rescans)
//   - TF card info and write/read speed test (button B runs the test)
//   - A/B button border feedback
//   - hold A+B to power off
//
// Board: m5stack:esp32:m5stack_paperdiy, PSRAM: OPI PSRAM
// Libraries: M5Unified, M5GFX
#include <M5Unified.h>

#include "Apps.h"
#include "Hal.h"
#include "ImgBg.h"

// The ESP-IDF demo runs on a 16 KB main task.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static const char* const FIRMWARE_VERSION = "V1.0";

static AppBattery appBattery;
static AppWifiScan appWifiScan;
static AppTfCard appTfCard;
static AppShutdown appShutdown;
static ButtonFeedback buttonFeedback;

static void drawBackground()
{
    auto& hal = GetHAL();
    DisplayGuard guard(hal);
    auto& display = hal.display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.fillScreen(TFT_WHITE);

    if (!display.drawPng(img_bg_png, img_bg_png_len, 0, 0)) {
        Serial.println("Failed to decode embedded background PNG");
    }
    hal.requestFullRedraw(m5gfx::epd_mode_t::epd_quality);
}

static void refreshDisplay()
{
    auto& hal = GetHAL();
    if (!hal.isRefreshRequested()) {
        return;
    }

    {
        DisplayGuard guard(hal);
        hal.display.setEpdMode(hal.isFullRedrawRequested() ? hal.fullRedrawMode() : hal.partialRefreshMode());
        hal.flushDisplay();
    }
    hal.display.waitDisplay();
    hal.clearRefreshRequest();
}

static void updateApps()
{
    appBattery.update();
    appWifiScan.update();
    appTfCard.update();
    // Last, so its modal state is evaluated after the other apps each pass.
    appShutdown.update();
}

void setup()
{
    Serial.begin(115200);
    Serial.printf("PaperDIY firmware version: %s\n", FIRMWARE_VERSION);

    if (!GetHAL().init()) {
        while (true) {
            GetHAL().delay(1000);
        }
    }

    // A blank NVS shows the shipping logo once, records the marker, and powers
    // off before any normal application starts (see FACTORY_FIRST_BOOT_SHUTDOWN).
    AppShutdown::handleFactoryBoot();

    // Build the complete startup frame in memory before the first panel update.
    drawBackground();

    appBattery.begin();
    appWifiScan.begin();
    appTfCard.begin();
    appShutdown.begin();

    refreshDisplay();
    if (!buttonFeedback.begin()) {
        Serial.println("Button feedback task initialization failed");
    }
}

void loop()
{
    M5.update();
    updateApps();
    refreshDisplay();
    GetHAL().feedTheDog();
}
