// Demo apps. Each has begin() (once, after the background is drawn) and
// update() (every loop pass). They replace the ESP-IDF demo's mooncake apps.
#pragma once

#include "Hal.h"

#include <atomic>
#include <string>
#include <vector>

class AppBattery {
public:
    void begin();
    void update();

private:
    void startSampling(bool force_draw);
    void collectSample();
    void drawPendingVoltage();
    bool drawVoltage(int16_t battery_mv, bool force_draw);

    uint32_t _last_sample_ms     = 0;
    uint32_t _last_update_ms     = 0;
    int32_t _sample_sum          = 0;
    uint32_t _sample_count       = 0;
    uint32_t _valid_sample_count = 0;
    bool _sampling               = false;
    bool _force_draw             = false;
    bool _pending_draw           = false;
    bool _pending_force_draw     = false;
    int16_t _pending_mv          = -1;
    char _last_text[16]          = {};
};

class AppWifiScan {
public:
    void begin();
    void update();

private:
    struct WifiNetwork {
        std::string ssid;
        int32_t rssi;
    };

    void startScan();
    void finishScan(int count);
    void drawStatus(const char* text, bool large = false);
    void drawResults();

    bool _scan_running = false;
    std::vector<WifiNetwork> _networks;
};

class AppTfCard {
public:
    void begin();
    void update();

private:
    bool startBenchmark();
    static void benchmarkTask(void* argument);
    void updateCard(bool force_draw, bool retry_mount = false);
    void drawInfo();
    void drawStatus(const char* text);

    uint32_t _last_poll_ms = 0;
    bool _has_drawn        = false;
    TfCardStatus _last_status = TfCardStatus::NoCard;
    bool _test_running     = false;
    std::atomic<bool> _test_done{false};
    TfBenchmarkResult _benchmark;
};

// Draws a border around the A/B button labels while each is held. Runs in its
// own high-priority task so feedback is not delayed by the other apps.
class ButtonFeedback {
public:
    bool begin();

private:
    static void feedbackTask(void* argument);
    bool update(bool force_draw = false);
    void refreshUpdate();

    bool _initialized      = false;
    bool _button_a_pressed = false;
    bool _button_b_pressed = false;
    uint8_t _updated_buttons = 0;
};

// Hold A+B for 2 s to open a power-off dialog, keep holding for 5 s more to
// show the logo and cut power through the M5PM1.
class AppShutdown {
public:
    // First boot after a blank flash: show the logo once and power off, as the
    // factory firmware does. Disabled by FACTORY_FIRST_BOOT_SHUTDOWN.
    static void handleFactoryBoot();
    void begin() {}
    void update();

private:
    enum class State : uint8_t {
        Idle,
        Arming,
        Countdown,
        WaitForRelease,
    };

    bool showDialog();
    void drawCountdown(unsigned int seconds);
    void dismissDialog();
    [[noreturn]] void shutDown();
    static bool displayShutdownLogo();
    [[noreturn]] static void sendPowerOffCommand();

    State _state                  = State::Idle;
    uint32_t _arming_started_ms   = 0;
    uint32_t _countdown_started_ms = 0;
    unsigned int _last_countdown  = 0;
    uint16_t* _saved_pixels       = nullptr;
};
