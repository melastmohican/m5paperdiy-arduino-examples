// Board layer for the PaperDIY demo: display refresh bookkeeping, debounced
// A/B buttons and the TF card. Ported from the ESP-IDF demo's main/hal.
#pragma once

#include <M5Unified.h>
#include <atomic>
#include <string>

enum class ButtonId : uint8_t {
    A,
    B,
};

enum class TfCardStatus : uint8_t {
    NoCard,
    Mounted,
    MountFailed,
};

struct TfCardInfo {
    TfCardStatus status = TfCardStatus::NoCard;
    std::string type;
    std::string message;
    uint64_t capacity_bytes = 0;
};

struct TfBenchmarkResult {
    bool success     = false;
    float write_mb_s = 0.0f;
    float read_mb_s  = 0.0f;
    std::string message;
};

class Hal {
public:
    bool init();

    M5GFX& display = M5.Display;

    /* System */
    void delay(uint32_t ms);
    uint32_t millis();
    int16_t getBatteryVoltageMv();
    void feedTheDog();

    /* Button */
    bool isButtonPressed(ButtonId button) const;
    bool isButtonInteractionActive();
    bool wasButtonAClicked();
    bool wasButtonBClicked();
    void suppressButtonActionsUntilRelease();

    /* TF card */
    bool initTfCard();
    bool isTfCardInserted() const;
    const TfCardInfo& updateTfCard(bool retry_mount = false);
    const TfCardInfo& getTfCardInfo() const;
    TfBenchmarkResult benchmarkTfCard();

    /* Display */
    void lockDisplay();
    void unlockDisplay();
    void flushDisplay();
    bool flushDisplayRegion(int32_t x, int32_t y, int32_t width, int32_t height, m5gfx::epd_mode_t mode);
    void flushFullDisplay(m5gfx::epd_mode_t mode);
    void requestRefresh();
    void requestRefresh(int32_t x, int32_t y, int32_t width, int32_t height,
                        m5gfx::epd_mode_t mode = m5gfx::epd_mode_t::epd_text);
    void requestFullRedraw(m5gfx::epd_mode_t mode = m5gfx::epd_mode_t::epd_quality);
    bool isRefreshRequested() const;
    bool isFullRedrawRequested() const;
    m5gfx::epd_mode_t partialRefreshMode() const;
    m5gfx::epd_mode_t fullRedrawMode() const;
    void clearRefreshRequest();

    /* Shutdown */
    void beginShutdown();
    bool isShutdownInProgress() const;

private:
    bool initButtonInput();
    void scheduleButtonAction(ButtonId button);
    bool consumeButtonAction(ButtonId button);
    static void buttonInputTask(void* argument);

    std::atomic<uint8_t> _button_state_bits{0};
    std::atomic<bool> _suppress_button_actions_until_release{false};
    portMUX_TYPE _button_action_mux = portMUX_INITIALIZER_UNLOCKED;
    uint8_t _pending_button_action  = 0;
    TickType_t _button_action_deadline = 0;

    bool mountTfCard();
    void unmountTfCard();

    bool _tf_mounted = false;
    TfCardInfo _tf_card_info;

    bool _refresh_request                   = false;
    bool _full_redraw_request               = false;
    bool _partial_refresh_request           = false;
    int32_t _refresh_x                      = 0;
    int32_t _refresh_y                      = 0;
    int32_t _refresh_width                  = 0;
    int32_t _refresh_height                 = 0;
    m5gfx::epd_mode_t _partial_refresh_mode = m5gfx::epd_mode_t::epd_text;
    m5gfx::epd_mode_t _full_redraw_mode     = m5gfx::epd_mode_t::epd_quality;
    uint32_t _partial_refresh_count         = 0;
    SemaphoreHandle_t _display_mutex        = nullptr;

    std::atomic<bool> _shutdown_in_progress{false};
};

// Holds the display mutex for the lifetime of the object.
class DisplayGuard {
public:
    explicit DisplayGuard(Hal& hal) : _hal(hal) { _hal.lockDisplay(); }
    ~DisplayGuard() { _hal.unlockDisplay(); }

    DisplayGuard(const DisplayGuard&)            = delete;
    DisplayGuard& operator=(const DisplayGuard&) = delete;

private:
    Hal& _hal;
};

Hal& GetHAL();
