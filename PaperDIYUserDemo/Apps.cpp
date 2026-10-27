#include "Apps.h"

#include "ImgLogo.h"

#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_heap_caps.h>

#ifndef FACTORY_FIRST_BOOT_SHUTDOWN
#define FACTORY_FIRST_BOOT_SHUTDOWN 1
#endif

// The ESP-IDF demo uses LVGL's Montserrat fonts (added to M5GFX by a patch).
// Stock M5GFX does not ship them, so use the closest built-in FreeSans sizes.
#define FONT_14 (&fonts::FreeSans9pt7b)
#define FONT_18 (&fonts::FreeSans12pt7b)
#define FONT_20 (&fonts::FreeSansBold12pt7b)
#define FONT_28 (&fonts::FreeSansBold18pt7b)
#define FONT_36 (&fonts::FreeSansBold24pt7b)

/* -------------------------------------------------------------------------- */
/*                                  Battery                                   */
/* -------------------------------------------------------------------------- */
namespace {

constexpr uint32_t BATTERY_REFRESH_MS      = 10000;
constexpr uint32_t BATTERY_SAMPLE_DELAY_MS = 50;
constexpr uint32_t BATTERY_SAMPLE_COUNT    = 10;
constexpr int BATTERY_X                    = 159;
constexpr int BATTERY_Y                    = 888;
constexpr int BATTERY_W                    = 241 - BATTERY_X;
constexpr int BATTERY_H                    = 925 - BATTERY_Y;

}  // namespace

void AppBattery::begin()
{
    startSampling(true);
    while (_sampling) {
        GetHAL().delay(BATTERY_SAMPLE_DELAY_MS);
        collectSample();
    }
    drawPendingVoltage();
}

void AppBattery::update()
{
    if (_pending_draw) {
        drawPendingVoltage();
        return;
    }

    const uint32_t now_ms = GetHAL().millis();
    if (_sampling) {
        if (now_ms - _last_sample_ms >= BATTERY_SAMPLE_DELAY_MS) {
            collectSample();
        }
    } else if (now_ms - _last_update_ms >= BATTERY_REFRESH_MS) {
        startSampling(false);
    }
}

void AppBattery::startSampling(bool force_draw)
{
    _sample_sum         = 0;
    _sample_count       = 0;
    _valid_sample_count = 0;
    _sampling           = true;
    _force_draw         = force_draw;
    collectSample();
}

void AppBattery::collectSample()
{
    _last_sample_ms         = GetHAL().millis();
    const int16_t battery_mv = GetHAL().getBatteryVoltageMv();
    if (battery_mv > 0) {
        _sample_sum += battery_mv;
        ++_valid_sample_count;
    }
    ++_sample_count;

    if (_sample_count < BATTERY_SAMPLE_COUNT) {
        return;
    }

    const int16_t average_mv =
        _valid_sample_count > 0
            ? static_cast<int16_t>((_sample_sum + static_cast<int32_t>(_valid_sample_count) / 2) / _valid_sample_count)
            : -1;
    _sampling           = false;
    _last_update_ms     = _last_sample_ms;
    _pending_mv         = average_mv;
    _pending_force_draw = _pending_force_draw || _force_draw;
    _pending_draw       = true;
}

void AppBattery::drawPendingVoltage()
{
    if (!_pending_draw || GetHAL().isButtonInteractionActive()) {
        return;
    }
    if (drawVoltage(_pending_mv, _pending_force_draw)) {
        _pending_draw       = false;
        _pending_force_draw = false;
    }
}

bool AppBattery::drawVoltage(int16_t battery_mv, bool force_draw)
{
    char voltage_text[sizeof(_last_text)] = {};
    if (battery_mv > 0) {
        snprintf(voltage_text, sizeof(voltage_text), "%.2fV", battery_mv / 1000.0f);
    } else {
        snprintf(voltage_text, sizeof(voltage_text), "--.--V");
    }

    if (!force_draw && strcmp(voltage_text, _last_text) == 0) {
        return true;
    }
    auto& hal = GetHAL();
    DisplayGuard guard(hal);
    if (hal.isButtonInteractionActive()) {
        return false;
    }
    auto& display = hal.display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_text);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setTextSize(1.0f);
    display.setFont(FONT_20);
    display.fillRect(BATTERY_X, BATTERY_Y, BATTERY_W, BATTERY_H, TFT_WHITE);
    display.setClipRect(BATTERY_X, BATTERY_Y, BATTERY_W, BATTERY_H);
    display.drawString(voltage_text, BATTERY_X + BATTERY_W / 2, BATTERY_Y + BATTERY_H / 2);
    display.clearClipRect();
    snprintf(_last_text, sizeof(_last_text), "%s", voltage_text);

    if (!hal.isFullRedrawRequested()) {
        hal.requestRefresh(BATTERY_X, BATTERY_Y, BATTERY_W, BATTERY_H);
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                 Wi-Fi scan                                 */
/* -------------------------------------------------------------------------- */
namespace {

constexpr int WIFI_INFO_X         = 121;
constexpr int WIFI_INFO_Y         = 503;
constexpr int WIFI_INFO_W         = 418 - WIFI_INFO_X;
constexpr int WIFI_INFO_H         = 715 - WIFI_INFO_Y;
constexpr int WIFI_ROW_H          = 24;
constexpr int WIFI_LIST_PADDING_X = 8;
constexpr int WIFI_LIST_PADDING_Y = 9;
constexpr int WIFI_RSSI_W         = 72;

void requestWifiRefresh(m5gfx::epd_mode_t mode)
{
    if (!GetHAL().isFullRedrawRequested()) {
        GetHAL().requestRefresh(WIFI_INFO_X, WIFI_INFO_Y, WIFI_INFO_W, WIFI_INFO_H, mode);
    }
}

// Trims the SSID (on UTF-8 boundaries) until it fits, ending with ".".
std::string fitSsidText(const std::string& ssid, int max_width)
{
    auto& display    = GetHAL().display;
    std::string text = ssid.empty() ? "<hidden>" : ssid;
    if (display.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    while (!text.empty()) {
        size_t erase_at = text.size() - 1;
        while (erase_at > 0 && (static_cast<unsigned char>(text[erase_at]) & 0xC0) == 0x80) {
            --erase_at;
        }
        text.resize(erase_at);
        const std::string clipped = text + ".";
        if (display.textWidth(clipped.c_str()) <= max_width) {
            return clipped;
        }
    }
    return ".";
}

}  // namespace

void AppWifiScan::begin()
{
    startScan();
}

void AppWifiScan::update()
{
    if (!_scan_running) {
        if (GetHAL().wasButtonAClicked()) {
            startScan();
        }
        return;
    }

    const int16_t count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) {
        return;
    }

    _scan_running = false;
    if (count == WIFI_SCAN_FAILED) {
        Serial.println("[WifiScan] scan failed");
        WiFi.scanDelete();
        drawStatus("SCAN FAILED");
        return;
    }
    finishScan(count);
}

void AppWifiScan::startScan()
{
    if (_scan_running) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    // async, show hidden, active scan, 120 ms per channel
    if (WiFi.scanNetworks(true, true, false, 120) == WIFI_SCAN_FAILED) {
        Serial.println("[WifiScan] unable to start scan");
        drawStatus("SCAN START FAILED");
        return;
    }

    _scan_running = true;
    drawStatus("SCANNING...", true);
}

void AppWifiScan::finishScan(int count)
{
    _networks.clear();
    _networks.reserve(count);
    for (int i = 0; i < count; ++i) {
        _networks.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i)});
    }
    WiFi.scanDelete();
    std::sort(_networks.begin(), _networks.end(),
              [](const WifiNetwork& left, const WifiNetwork& right) { return left.rssi > right.rssi; });

    Serial.printf("[WifiScan] complete: %d networks\n", count);
    drawResults();
}

void AppWifiScan::drawStatus(const char* text, bool large)
{
    auto& hal = GetHAL();
    DisplayGuard guard(hal);
    auto& display = hal.display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_text);
    display.fillRect(WIFI_INFO_X, WIFI_INFO_Y, WIFI_INFO_W, WIFI_INFO_H, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setTextSize(1.0f);
    display.setFont(large ? FONT_20 : FONT_18);
    display.setClipRect(WIFI_INFO_X, WIFI_INFO_Y, WIFI_INFO_W, WIFI_INFO_H);
    display.drawString(text, WIFI_INFO_X + WIFI_INFO_W / 2, WIFI_INFO_Y + WIFI_INFO_H / 2);
    display.clearClipRect();
    requestWifiRefresh(m5gfx::epd_mode_t::epd_text);
}

void AppWifiScan::drawResults()
{
    auto& hal = GetHAL();
    DisplayGuard guard(hal);
    auto& display = hal.display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.fillRect(WIFI_INFO_X, WIFI_INFO_Y, WIFI_INFO_W, WIFI_INFO_H, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(1.0f);
    display.setFont(FONT_14);
    display.setClipRect(WIFI_INFO_X, WIFI_INFO_Y, WIFI_INFO_W, WIFI_INFO_H);

    if (_networks.empty()) {
        display.setTextDatum(middle_center);
        display.drawString("NO NETWORKS", WIFI_INFO_X + WIFI_INFO_W / 2, WIFI_INFO_Y + WIFI_INFO_H / 2);
    } else {
        const int visible_rows = (WIFI_INFO_H - WIFI_LIST_PADDING_Y * 2) / WIFI_ROW_H;
        const int rssi_right   = WIFI_INFO_X + WIFI_INFO_W - 8;
        const int ssid_x       = WIFI_INFO_X + WIFI_LIST_PADDING_X;
        const int ssid_w       = rssi_right - WIFI_RSSI_W - ssid_x - 4;

        for (int row = 0; row < visible_rows && row < static_cast<int>(_networks.size()); ++row) {
            const auto& network    = _networks[row];
            const int row_y        = WIFI_INFO_Y + WIFI_LIST_PADDING_Y + row * WIFI_ROW_H;
            const int row_center_y = row_y + WIFI_ROW_H / 2;
            const std::string ssid = fitSsidText(network.ssid, ssid_w);
            char rssi_text[16]     = {};
            snprintf(rssi_text, sizeof(rssi_text), "%lddBm", static_cast<long>(network.rssi));

            display.setTextDatum(middle_left);
            display.drawString(ssid.c_str(), ssid_x, row_center_y);
            display.setTextDatum(middle_right);
            display.drawString(rssi_text, rssi_right, row_center_y);
        }
    }

    display.clearClipRect();
    requestWifiRefresh(m5gfx::epd_mode_t::epd_quality);
}

/* -------------------------------------------------------------------------- */
/*                                  TF card                                   */
/* -------------------------------------------------------------------------- */
namespace {

constexpr uint32_t TF_POLL_MS       = 500;
constexpr int TF_INFO_X             = 121;
constexpr int TF_INFO_Y             = 730;
constexpr int TF_INFO_W             = 418 - TF_INFO_X;
constexpr int TF_INFO_H             = 831 - TF_INFO_Y;
constexpr int TF_CONTENT_LEFT       = TF_INFO_X + 7;
constexpr int TF_CONTENT_RIGHT      = TF_INFO_X + TF_INFO_W - 7;
constexpr int TF_CONTENT_W          = TF_CONTENT_RIGHT - TF_CONTENT_LEFT;
constexpr int TF_COLUMN_GAP         = 14;
constexpr int TF_COLUMN_W           = (TF_CONTENT_W - TF_COLUMN_GAP) / 2;
constexpr int TF_RIGHT_COLUMN_X     = TF_CONTENT_LEFT + TF_COLUMN_W + TF_COLUMN_GAP;
constexpr int TF_LEFT_COLUMN_CENTER = TF_CONTENT_LEFT + TF_COLUMN_W / 2;
constexpr int TF_RIGHT_COLUMN_CENTER = TF_RIGHT_COLUMN_X + TF_COLUMN_W / 2;
constexpr int TF_HEADER_LEFT_X      = TF_CONTENT_LEFT + 13;
constexpr int TF_HEADER_RIGHT_X     = TF_CONTENT_RIGHT - 13;
constexpr int TF_HEADER_Y           = TF_INFO_Y + 20;
constexpr int TF_DIVIDER_Y          = TF_INFO_Y + 40;
constexpr int TF_SPEED_LABEL_Y      = TF_INFO_Y + 55;
constexpr int TF_SPEED_VALUE_Y      = TF_INFO_Y + 79;
constexpr uint16_t TF_DIVIDER_COLOR = 0xA514;

std::string formatCapacity(uint64_t bytes)
{
    char text[24] = {};
    snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return text;
}

std::string formatSpeed(float speed)
{
    char text[32] = {};
    snprintf(text, sizeof(text), "%.2f MB/s", static_cast<double>(speed));
    return text;
}

void requestTfRefresh(m5gfx::epd_mode_t mode)
{
    if (!GetHAL().isFullRedrawRequested()) {
        GetHAL().requestRefresh(TF_INFO_X, TF_INFO_Y, TF_INFO_W, TF_INFO_H, mode);
    }
}

}  // namespace

void AppTfCard::begin()
{
    updateCard(true);
}

void AppTfCard::update()
{
    if (_test_done.exchange(false, std::memory_order_acq_rel)) {
        _test_running = false;
        drawInfo();
        return;
    }

    if (!_test_running && GetHAL().wasButtonBClicked()) {
        updateCard(false, true);
        if (GetHAL().getTfCardInfo().status == TfCardStatus::Mounted) {
            drawStatus("TESTING...");
            if (!startBenchmark()) {
                _benchmark.message = "TASK FAILED";
                drawInfo();
            }
        }
        return;
    }

    if (_test_running) {
        return;
    }

    if (GetHAL().millis() - _last_poll_ms >= TF_POLL_MS) {
        updateCard(false);
    }
}

bool AppTfCard::startBenchmark()
{
    _test_done.store(false, std::memory_order_relaxed);
    _test_running = true;
    if (xTaskCreate(benchmarkTask, "tf_benchmark", 6144, this, tskIDLE_PRIORITY + 2, nullptr) != pdPASS) {
        _test_running = false;
        return false;
    }
    return true;
}

void AppTfCard::benchmarkTask(void* argument)
{
    auto* app       = static_cast<AppTfCard*>(argument);
    app->_benchmark = GetHAL().benchmarkTfCard();
    app->_test_done.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}

void AppTfCard::updateCard(bool force_draw, bool retry_mount)
{
    _last_poll_ms               = GetHAL().millis();
    const TfCardStatus previous = GetHAL().getTfCardInfo().status;
    const TfCardInfo& info      = GetHAL().updateTfCard(retry_mount);
    if (info.status != previous) {
        _benchmark = {};
    }
    if (force_draw || !_has_drawn || info.status != _last_status) {
        _last_status = info.status;
        drawInfo();
    }
}

void AppTfCard::drawInfo()
{
    const TfCardInfo& info = GetHAL().getTfCardInfo();
    auto& hal              = GetHAL();
    DisplayGuard guard(hal);
    auto& display = hal.display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.fillRect(TF_INFO_X, TF_INFO_Y, TF_INFO_W, TF_INFO_H, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextSize(1.0f);
    display.setClipRect(TF_INFO_X, TF_INFO_Y, TF_INFO_W, TF_INFO_H);

    if (info.status == TfCardStatus::Mounted) {
        const std::string capacity = formatCapacity(info.capacity_bytes);

        // Header: card type on the left, capacity on the right.
        display.setFont(FONT_18);
        display.setTextDatum(middle_left);
        display.setClipRect(TF_CONTENT_LEFT, TF_INFO_Y, TF_COLUMN_W, TF_DIVIDER_Y - TF_INFO_Y);
        display.drawString(info.type.c_str(), TF_HEADER_LEFT_X, TF_HEADER_Y);

        display.setTextDatum(middle_right);
        display.setClipRect(TF_RIGHT_COLUMN_X, TF_INFO_Y, TF_COLUMN_W, TF_DIVIDER_Y - TF_INFO_Y);
        display.drawString(capacity.c_str(), TF_HEADER_RIGHT_X, TF_HEADER_Y);

        display.setClipRect(TF_INFO_X, TF_INFO_Y, TF_INFO_W, TF_INFO_H);
        display.drawFastHLine(TF_CONTENT_LEFT, TF_DIVIDER_Y, TF_CONTENT_W, TF_DIVIDER_COLOR);

        if (!_benchmark.message.empty() && !_benchmark.success) {
            display.setTextDatum(middle_center);
            display.setFont(FONT_14);
            display.drawString("TEST FAILED", TF_INFO_X + TF_INFO_W / 2, TF_SPEED_LABEL_Y);
            display.setFont(FONT_18);
            display.drawString(_benchmark.message.c_str(), TF_INFO_X + TF_INFO_W / 2, TF_SPEED_VALUE_Y);
        } else {
            // Each speed cell has its own clip rectangle to avoid cross-column overlap.
            display.drawFastVLine(TF_INFO_X + TF_INFO_W / 2, TF_DIVIDER_Y + 8,
                                  TF_INFO_H - (TF_DIVIDER_Y - TF_INFO_Y) - 15, TF_DIVIDER_COLOR);
            display.setTextDatum(middle_center);
            display.setFont(FONT_14);
            display.setClipRect(TF_CONTENT_LEFT, TF_DIVIDER_Y + 1, TF_COLUMN_W,
                                TF_INFO_Y + TF_INFO_H - TF_DIVIDER_Y - 1);
            display.drawString("WRITE", TF_LEFT_COLUMN_CENTER, TF_SPEED_LABEL_Y);
            display.setFont(FONT_18);
            const std::string write_speed = _benchmark.success ? formatSpeed(_benchmark.write_mb_s) : "-- MB/s";
            display.drawString(write_speed.c_str(), TF_LEFT_COLUMN_CENTER, TF_SPEED_VALUE_Y);

            display.setClipRect(TF_RIGHT_COLUMN_X, TF_DIVIDER_Y + 1, TF_COLUMN_W,
                                TF_INFO_Y + TF_INFO_H - TF_DIVIDER_Y - 1);
            display.setFont(FONT_14);
            display.drawString("READ", TF_RIGHT_COLUMN_CENTER, TF_SPEED_LABEL_Y);
            display.setFont(FONT_18);
            const std::string read_speed = _benchmark.success ? formatSpeed(_benchmark.read_mb_s) : "-- MB/s";
            display.drawString(read_speed.c_str(), TF_RIGHT_COLUMN_CENTER, TF_SPEED_VALUE_Y);
        }
    } else {
        display.setFont(FONT_20);
        display.setTextDatum(middle_center);
        const char* text = info.status == TfCardStatus::NoCard ? "NO TF CARD" : "TF INIT FAILED";
        display.drawString(text, TF_INFO_X + TF_INFO_W / 2, TF_INFO_Y + TF_INFO_H / 2);
    }

    display.clearClipRect();
    requestTfRefresh(m5gfx::epd_mode_t::epd_quality);
    _has_drawn = true;
}

void AppTfCard::drawStatus(const char* text)
{
    auto& hal = GetHAL();
    DisplayGuard guard(hal);
    auto& display = hal.display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_text);
    display.fillRect(TF_INFO_X, TF_INFO_Y, TF_INFO_W, TF_INFO_H, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setTextSize(1.0f);
    display.setFont(FONT_18);
    display.setClipRect(TF_INFO_X, TF_INFO_Y, TF_INFO_W, TF_INFO_H);
    display.drawString(text, TF_INFO_X + TF_INFO_W / 2, TF_INFO_Y + TF_INFO_H / 2);
    display.clearClipRect();
    requestTfRefresh(m5gfx::epd_mode_t::epd_text);
}

/* -------------------------------------------------------------------------- */
/*                              Button feedback                               */
/* -------------------------------------------------------------------------- */
namespace {

constexpr int BORDER_X             = 9;
constexpr int BORDER_A_Y           = 620;
constexpr int BORDER_B_Y           = 731;
constexpr int BORDER_RIGHT         = 119;
constexpr int BORDER_A_BOTTOM      = 686;
constexpr int BORDER_B_BOTTOM      = 797;
constexpr int BORDER_H             = BORDER_A_BOTTOM - BORDER_A_Y + 1;
constexpr int BORDER_THICKNESS     = 2;
constexpr int BORDER_RIGHT_RADIUS  = 17;
constexpr int BORDER_REFRESH_X     = BORDER_X & ~3;
constexpr int BORDER_REFRESH_RIGHT = (BORDER_RIGHT + 4) & ~3;
constexpr int BORDER_REFRESH_W     = BORDER_REFRESH_RIGHT - BORDER_REFRESH_X;
constexpr unsigned int FEEDBACK_A_MASK = 1U << 0;
constexpr unsigned int FEEDBACK_B_MASK = 1U << 1;

static_assert(BORDER_B_Y + BORDER_H - 1 == BORDER_B_BOTTOM, "A and B borders must be the same height");

void drawRightCorners(M5GFX& display, int center_x, int top_center_y, int bottom_center_y, uint16_t color)
{
    const int inner_radius = BORDER_RIGHT_RADIUS - BORDER_THICKNESS + 1;
    display.fillArc(center_x, top_center_y, BORDER_RIGHT_RADIUS, inner_radius, 270.0f, 360.0f, color);
    display.fillArc(center_x, bottom_center_y, BORDER_RIGHT_RADIUS, inner_radius, 0.0f, 90.0f, color);
}

void drawBorder(int y, bool pressed)
{
    auto& display = GetHAL().display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    const uint16_t color     = pressed ? TFT_BLACK : TFT_WHITE;
    const int right_center_x = BORDER_RIGHT - BORDER_RIGHT_RADIUS;
    const int bottom         = y + BORDER_H - 1;

    drawRightCorners(display, right_center_x, y + BORDER_RIGHT_RADIUS, bottom - BORDER_RIGHT_RADIUS, color);

    display.fillRect(BORDER_X, y, right_center_x - BORDER_X + 1, BORDER_THICKNESS, color);
    display.fillRect(BORDER_X, bottom - BORDER_THICKNESS + 1, right_center_x - BORDER_X + 1, BORDER_THICKNESS, color);
    display.fillRect(BORDER_X, y, BORDER_THICKNESS, BORDER_H, color);
    display.fillRect(BORDER_RIGHT - BORDER_THICKNESS + 1, y + BORDER_RIGHT_RADIUS, BORDER_THICKNESS,
                     BORDER_H - BORDER_RIGHT_RADIUS * 2, color);
}

}  // namespace

bool ButtonFeedback::begin()
{
    {
        DisplayGuard guard(GetHAL());
        update(true);
        refreshUpdate();
    }
    GetHAL().display.waitDisplay();

    return xTaskCreatePinnedToCore(feedbackTask, "button_feedback", 4096, this, tskIDLE_PRIORITY + 3, nullptr, 1) ==
           pdPASS;
}

void ButtonFeedback::feedbackTask(void* argument)
{
    auto* feedback = static_cast<ButtonFeedback*>(argument);
    while (true) {
        auto& hal = GetHAL();
        if (hal.isShutdownInProgress()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        bool changed = false;
        {
            DisplayGuard guard(hal);
            if (!hal.isShutdownInProgress()) {
                changed = feedback->update();
                if (changed) {
                    feedback->refreshUpdate();
                }
            }
        }

        if (changed) {
            hal.display.waitDisplay();
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

bool ButtonFeedback::update(bool force_draw)
{
    auto& hal        = GetHAL();
    _updated_buttons = 0;

    if (force_draw || !_initialized) {
        _button_a_pressed = hal.isButtonPressed(ButtonId::A);
        _button_b_pressed = hal.isButtonPressed(ButtonId::B);
        if (_button_a_pressed) {
            drawBorder(BORDER_A_Y, true);
            _updated_buttons |= FEEDBACK_A_MASK;
        }
        if (_button_b_pressed) {
            drawBorder(BORDER_B_Y, true);
            _updated_buttons |= FEEDBACK_B_MASK;
        }
        _initialized = true;
        return _updated_buttons != 0;
    }

    const bool button_a_pressed = hal.isButtonPressed(ButtonId::A);
    if (button_a_pressed != _button_a_pressed) {
        _button_a_pressed = button_a_pressed;
        drawBorder(BORDER_A_Y, button_a_pressed);
        _updated_buttons |= FEEDBACK_A_MASK;
    }

    const bool button_b_pressed = hal.isButtonPressed(ButtonId::B);
    if (button_b_pressed != _button_b_pressed) {
        _button_b_pressed = button_b_pressed;
        drawBorder(BORDER_B_Y, button_b_pressed);
        _updated_buttons |= FEEDBACK_B_MASK;
    }

    return _updated_buttons != 0;
}

void ButtonFeedback::refreshUpdate()
{
    if (_updated_buttons == 0) {
        return;
    }

    auto& hal                   = GetHAL();
    bool full_refresh_completed = false;
    if ((_updated_buttons & FEEDBACK_A_MASK) != 0) {
        full_refresh_completed = hal.flushDisplayRegion(BORDER_REFRESH_X, BORDER_A_Y, BORDER_REFRESH_W, BORDER_H,
                                                        m5gfx::epd_mode_t::epd_fastest);
    }
    if (!full_refresh_completed && (_updated_buttons & FEEDBACK_B_MASK) != 0) {
        hal.flushDisplayRegion(BORDER_REFRESH_X, BORDER_B_Y, BORDER_REFRESH_W, BORDER_H,
                               m5gfx::epd_mode_t::epd_fastest);
    }
    _updated_buttons = 0;
}

/* -------------------------------------------------------------------------- */
/*                                  Shutdown                                  */
/* -------------------------------------------------------------------------- */
namespace {

constexpr char NVS_NAMESPACE[]    = "paperdiy";
constexpr char NVS_FACTORY_FLAG[] = "factory_done";

constexpr uint32_t ARMING_TIME_MS    = 2000;
constexpr uint32_t COUNTDOWN_TIME_MS = 5000;

constexpr int DIALOG_W      = 404;
constexpr int DIALOG_H      = 224;
constexpr int DIALOG_X      = (540 - DIALOG_W) / 2;
constexpr int DIALOG_Y      = (960 - DIALOG_H) / 2;
constexpr int DIALOG_RADIUS = 6;
constexpr int COUNTDOWN_X   = DIALOG_X + 50;
constexpr int COUNTDOWN_Y   = DIALOG_Y + 119;
constexpr int COUNTDOWN_W   = DIALOG_W - 100;
constexpr int COUNTDOWN_H   = 54;

bool areBothButtonsPressed()
{
    const auto& hal = GetHAL();
    return hal.isButtonPressed(ButtonId::A) && hal.isButtonPressed(ButtonId::B);
}

}  // namespace

void AppShutdown::handleFactoryBoot()
{
#if FACTORY_FIRST_BOOT_SHUTDOWN
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[Shutdown] unable to open factory NVS");
        return;
    }
    if (prefs.getUChar(NVS_FACTORY_FLAG, 0) != 0) {
        prefs.end();
        return;
    }

    Serial.println("[Shutdown] first factory boot: displaying shutdown logo");
    GetHAL().beginShutdown();
    if (displayShutdownLogo()) {
        prefs.putUChar(NVS_FACTORY_FLAG, 1);
    }
    prefs.end();
    sendPowerOffCommand();
#endif
}

void AppShutdown::update()
{
    auto& hal                = GetHAL();
    const uint32_t now_ms    = hal.millis();
    const bool both_pressed  = areBothButtonsPressed();

    switch (_state) {
        case State::Idle:
            if (both_pressed) {
                _arming_started_ms = now_ms;
                _state             = State::Arming;
            }
            break;

        case State::Arming:
            if (!both_pressed) {
                _state = State::Idle;
            } else if (now_ms - _arming_started_ms >= ARMING_TIME_MS) {
                hal.suppressButtonActionsUntilRelease();
                if (showDialog()) {
                    _countdown_started_ms = hal.millis();
                    _last_countdown       = 5;
                    _state                = State::Countdown;
                } else {
                    _state = State::WaitForRelease;
                }
            }
            break;

        case State::Countdown: {
            if (!both_pressed) {
                dismissDialog();
                _state = State::WaitForRelease;
                break;
            }

            const uint32_t elapsed_ms = now_ms - _countdown_started_ms;
            if (elapsed_ms >= COUNTDOWN_TIME_MS) {
                shutDown();
            }

            const unsigned int seconds = 5U - elapsed_ms / 1000U;
            if (seconds != _last_countdown) {
                drawCountdown(seconds);
                _last_countdown = seconds;
            }
            break;
        }

        case State::WaitForRelease:
            if (!hal.isButtonPressed(ButtonId::A) && !hal.isButtonPressed(ButtonId::B)) {
                _state = State::Idle;
            }
            break;
    }
}

bool AppShutdown::showDialog()
{
    constexpr size_t pixel_count = static_cast<size_t>(DIALOG_W) * DIALOG_H;
    _saved_pixels = static_cast<uint16_t*>(
        heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (_saved_pixels == nullptr) {
        _saved_pixels = static_cast<uint16_t*>(heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_8BIT));
    }
    if (_saved_pixels == nullptr) {
        Serial.println("[Shutdown] unable to allocate dialog background buffer");
        return false;
    }

    auto& hal     = GetHAL();
    auto& display = hal.display;
    display.waitDisplay();
    {
        DisplayGuard guard(hal);
        display.readRect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, _saved_pixels);
        display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
        display.fillRoundRect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, DIALOG_RADIUS, TFT_WHITE);
        display.drawRoundRect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, DIALOG_RADIUS, TFT_BLACK);
        display.drawRoundRect(DIALOG_X + 1, DIALOG_Y + 1, DIALOG_W - 2, DIALOG_H - 2, DIALOG_RADIUS - 1, TFT_BLACK);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.setTextDatum(middle_center);
        display.setTextSize(1.0f);
        display.setClipRect(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H);

        display.setFont(FONT_28);
        display.drawString("POWER OFF", DIALOG_X + DIALOG_W / 2, DIALOG_Y + 34);
        display.drawFastHLine(DIALOG_X + 24, DIALOG_Y + 59, DIALOG_W - 48, 0xA514);
        display.setFont(FONT_20);
        display.drawString("KEEP HOLDING A + B", DIALOG_X + DIALOG_W / 2, DIALOG_Y + 87);
        display.setFont(FONT_14);
        display.drawString("POWER OFF IN", DIALOG_X + DIALOG_W / 2, DIALOG_Y + 113);
        display.setFont(FONT_36);
        display.drawString("5 SECONDS", DIALOG_X + DIALOG_W / 2, DIALOG_Y + 146);
        display.setFont(FONT_14);
        display.drawString("RELEASE EITHER BUTTON TO CANCEL", DIALOG_X + DIALOG_W / 2, DIALOG_Y + 196);
        display.clearClipRect();
        hal.flushDisplayRegion(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, m5gfx::epd_mode_t::epd_quality);
    }
    display.waitDisplay();
    return true;
}

void AppShutdown::drawCountdown(unsigned int seconds)
{
    char text[24] = {};
    snprintf(text, sizeof(text), "%u SECONDS", seconds);

    auto& hal     = GetHAL();
    auto& display = hal.display;
    display.waitDisplay();
    {
        DisplayGuard guard(hal);
        display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
        display.fillRect(COUNTDOWN_X, COUNTDOWN_Y, COUNTDOWN_W, COUNTDOWN_H, TFT_WHITE);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
        display.setTextDatum(middle_center);
        display.setTextSize(1.0f);
        display.setFont(FONT_36);
        display.setClipRect(COUNTDOWN_X, COUNTDOWN_Y, COUNTDOWN_W, COUNTDOWN_H);
        display.drawString(text, COUNTDOWN_X + COUNTDOWN_W / 2, COUNTDOWN_Y + COUNTDOWN_H / 2);
        display.clearClipRect();
        hal.flushDisplayRegion(COUNTDOWN_X, COUNTDOWN_Y, COUNTDOWN_W, COUNTDOWN_H, m5gfx::epd_mode_t::epd_fastest);
    }
    display.waitDisplay();
}

void AppShutdown::dismissDialog()
{
    if (_saved_pixels == nullptr) {
        return;
    }

    auto& hal     = GetHAL();
    auto& display = hal.display;
    display.waitDisplay();
    {
        DisplayGuard guard(hal);
        display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
        display.pushImage(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, _saved_pixels);
        hal.flushDisplayRegion(DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, m5gfx::epd_mode_t::epd_quality);
    }
    display.waitDisplay();
    heap_caps_free(_saved_pixels);
    _saved_pixels = nullptr;
}

[[noreturn]] void AppShutdown::shutDown()
{
    GetHAL().beginShutdown();
    displayShutdownLogo();
    sendPowerOffCommand();
}

bool AppShutdown::displayShutdownLogo()
{
    auto& hal     = GetHAL();
    auto& display = hal.display;
    bool drawn    = false;
    display.waitDisplay();
    {
        DisplayGuard guard(hal);
        display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
        display.fillScreen(TFT_WHITE);
        drawn = display.drawPng(img_logo_png, img_logo_png_len, 0, 0);
        if (!drawn) {
            Serial.println("[Shutdown] failed to decode shutdown logo PNG");
        }
        hal.flushFullDisplay(m5gfx::epd_mode_t::epd_quality);
    }
    display.waitDisplay();
    return drawn;
}

[[noreturn]] void AppShutdown::sendPowerOffCommand()
{
    Serial.println("[Shutdown] sending M5PM1 shutdown command");
    while (true) {
        if (!M5.Power.M5pm1.powerOff()) {
            Serial.println("[Shutdown] M5PM1 shutdown command failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
