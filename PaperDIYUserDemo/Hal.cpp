#include "Hal.h"

#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <esp_timer.h>

namespace {

// Buttons (active low)
constexpr int BUTTON_A_PIN                = 4;
constexpr int BUTTON_B_PIN                = 3;
constexpr uint8_t BUTTON_A_MASK           = 1U << 0;
constexpr uint8_t BUTTON_B_MASK           = 1U << 1;
constexpr uint32_t BUTTON_POLL_MS         = 2;
constexpr uint32_t BUTTON_DEBOUNCE_MS     = 10;
constexpr uint32_t BUTTON_ACTION_DELAY_MS = 800;
constexpr uint8_t BUTTON_ACTION_NONE      = 0;
constexpr uint8_t BUTTON_ACTION_A         = 1;
constexpr uint8_t BUTTON_ACTION_B         = 2;

// TF card
constexpr int PIN_MISO              = 40;
constexpr int PIN_MOSI              = 38;
constexpr int PIN_SCLK              = 39;
constexpr int PIN_CS                = 47;
constexpr int PIN_DET               = 48;
constexpr size_t TEST_SIZE_BYTES    = 1024 * 1024;
constexpr size_t TEST_BUFFER_SIZE   = 16 * 1024;

// Display
constexpr uint32_t PARTIAL_REFRESH_LIMIT = 40;

uint8_t readButtonBits()
{
    return (digitalRead(BUTTON_A_PIN) == LOW ? BUTTON_A_MASK : 0) |
           (digitalRead(BUTTON_B_PIN) == LOW ? BUTTON_B_MASK : 0);
}

uint8_t testPattern(size_t i)
{
    return static_cast<uint8_t>((i * 31U + 17U) & 0xFFU);
}

float throughputMbPerSecond(size_t bytes, int64_t elapsed_us)
{
    if (elapsed_us <= 0) {
        return 0.0f;
    }
    return static_cast<float>(bytes) * 1000000.0f / (static_cast<float>(elapsed_us) * 1024.0f * 1024.0f);
}

}  // namespace

Hal& GetHAL()
{
    static Hal hal;
    return hal;
}

/* -------------------------------------------------------------------------- */
/*                                    Init                                    */
/* -------------------------------------------------------------------------- */
bool Hal::init()
{
    auto config          = M5.config();
    config.clear_display = false;
    config.internal_imu  = false;
    config.internal_mic  = false;
    config.internal_spk  = false;
    M5.begin(config);

    _display_mutex = xSemaphoreCreateMutex();
    if (_display_mutex == nullptr) {
        Serial.println("[HAL] Display mutex initialization failed");
        return false;
    }

    if (!initButtonInput()) {
        Serial.println("[HAL] Button input task initialization failed");
    }

    if (M5.getDisplayCount() == 0 || display.width() <= 0 || display.height() <= 0) {
        Serial.println("[HAL] Display initialization failed");
        return false;
    }

    display.setRotation(0);
    display.setAutoDisplay(false);
    if (!initTfCard()) {
        Serial.println("[HAL] TF card detect initialization failed");
    }
    Serial.printf("[HAL] Display ready: %dx%d\n", display.width(), display.height());
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
void Hal::delay(uint32_t ms)
{
    m5gfx::delay(ms);
}

uint32_t Hal::millis()
{
    return m5gfx::millis();
}

int16_t Hal::getBatteryVoltageMv()
{
    return M5.Power.M5pm1.getBatteryVoltage();
}

void Hal::feedTheDog()
{
    vTaskDelay(pdMS_TO_TICKS(5));
}

void Hal::lockDisplay()
{
    xSemaphoreTake(_display_mutex, portMAX_DELAY);
}

void Hal::unlockDisplay()
{
    xSemaphoreGive(_display_mutex);
}

/* -------------------------------------------------------------------------- */
/*                                Button Input                                */
/* -------------------------------------------------------------------------- */
bool Hal::initButtonInput()
{
    _button_state_bits.store(readButtonBits(), std::memory_order_release);
    return xTaskCreatePinnedToCore(buttonInputTask, "button_input", 2048, this, tskIDLE_PRIORITY + 4, nullptr, 1) ==
           pdPASS;
}

void Hal::buttonInputTask(void* argument)
{
    auto* hal              = static_cast<Hal*>(argument);
    uint8_t stable_bits    = readButtonBits();
    uint8_t candidate_bits = stable_bits;
    TickType_t candidate_since = xTaskGetTickCount();
    bool chord_active      = (stable_bits & (BUTTON_A_MASK | BUTTON_B_MASK)) == (BUTTON_A_MASK | BUTTON_B_MASK);
    hal->_button_state_bits.store(stable_bits, std::memory_order_release);

    while (true) {
        const TickType_t now  = xTaskGetTickCount();
        const uint8_t raw_bits = readButtonBits();
        if (hal->_shutdown_in_progress.load(std::memory_order_acquire)) {
            stable_bits     = raw_bits;
            candidate_bits  = raw_bits;
            candidate_since = now;
            chord_active    = false;
            hal->_button_state_bits.store(stable_bits, std::memory_order_release);
            vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
            continue;
        }
        if (raw_bits != candidate_bits) {
            candidate_bits  = raw_bits;
            candidate_since = now;
        } else if (candidate_bits != stable_bits && now - candidate_since >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
            const uint8_t previous_bits = stable_bits;
            const uint8_t changed_bits  = stable_bits ^ candidate_bits;
            stable_bits                 = candidate_bits;
            hal->_button_state_bits.store(stable_bits, std::memory_order_release);

            const uint8_t both_buttons = BUTTON_A_MASK | BUTTON_B_MASK;
            if ((stable_bits & both_buttons) == both_buttons) {
                chord_active = true;
            }

            const uint8_t released_bits = changed_bits & previous_bits;
            if (released_bits != 0) {
                if (chord_active) {
                    // Chord: only act once both buttons are up, and not if the
                    // shutdown gesture already consumed it.
                    if ((stable_bits & both_buttons) == 0) {
                        const bool suppressed =
                            hal->_suppress_button_actions_until_release.exchange(false, std::memory_order_acq_rel);
                        if (!suppressed) {
                            hal->scheduleButtonAction((released_bits & BUTTON_B_MASK) != 0 ? ButtonId::B : ButtonId::A);
                        }
                        chord_active = false;
                    }
                } else {
                    if ((released_bits & BUTTON_A_MASK) != 0) {
                        hal->scheduleButtonAction(ButtonId::A);
                    }
                    if ((released_bits & BUTTON_B_MASK) != 0) {
                        hal->scheduleButtonAction(ButtonId::B);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

bool Hal::isButtonPressed(ButtonId button) const
{
    const uint8_t mask = button == ButtonId::A ? BUTTON_A_MASK : BUTTON_B_MASK;
    return (_button_state_bits.load(std::memory_order_acquire) & mask) != 0;
}

bool Hal::isButtonInteractionActive()
{
    if (_button_state_bits.load(std::memory_order_acquire) != 0) {
        return true;
    }

    portENTER_CRITICAL(&_button_action_mux);
    const bool active = _pending_button_action != BUTTON_ACTION_NONE;
    portEXIT_CRITICAL(&_button_action_mux);
    return active;
}

void Hal::scheduleButtonAction(ButtonId button)
{
    if (_shutdown_in_progress.load(std::memory_order_acquire)) {
        return;
    }

    portENTER_CRITICAL(&_button_action_mux);
    _pending_button_action  = button == ButtonId::A ? BUTTON_ACTION_A : BUTTON_ACTION_B;
    _button_action_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BUTTON_ACTION_DELAY_MS);
    portEXIT_CRITICAL(&_button_action_mux);
}

bool Hal::consumeButtonAction(ButtonId button)
{
    if (_shutdown_in_progress.load(std::memory_order_acquire)) {
        return false;
    }

    const uint8_t expected_action = button == ButtonId::A ? BUTTON_ACTION_A : BUTTON_ACTION_B;
    bool clicked                  = false;

    portENTER_CRITICAL(&_button_action_mux);
    if (_pending_button_action == expected_action && _button_state_bits.load(std::memory_order_relaxed) == 0 &&
        static_cast<int32_t>(xTaskGetTickCount() - _button_action_deadline) >= 0) {
        _pending_button_action = BUTTON_ACTION_NONE;
        clicked                = true;
    }
    portEXIT_CRITICAL(&_button_action_mux);
    return clicked;
}

bool Hal::wasButtonAClicked()
{
    return consumeButtonAction(ButtonId::A);
}

bool Hal::wasButtonBClicked()
{
    return consumeButtonAction(ButtonId::B);
}

void Hal::suppressButtonActionsUntilRelease()
{
    _suppress_button_actions_until_release.store(true, std::memory_order_release);
    portENTER_CRITICAL(&_button_action_mux);
    _pending_button_action = BUTTON_ACTION_NONE;
    portEXIT_CRITICAL(&_button_action_mux);
}

void Hal::beginShutdown()
{
    _shutdown_in_progress.store(true, std::memory_order_release);
    _suppress_button_actions_until_release.store(true, std::memory_order_release);
    portENTER_CRITICAL(&_button_action_mux);
    _pending_button_action = BUTTON_ACTION_NONE;
    portEXIT_CRITICAL(&_button_action_mux);
}

bool Hal::isShutdownInProgress() const
{
    return _shutdown_in_progress.load(std::memory_order_acquire);
}

/* -------------------------------------------------------------------------- */
/*                                  TF Card                                   */
/* -------------------------------------------------------------------------- */
bool Hal::initTfCard()
{
    pinMode(PIN_DET, INPUT_PULLUP);
    return true;
}

bool Hal::isTfCardInserted() const
{
    return digitalRead(PIN_DET) == LOW;
}

const TfCardInfo& Hal::updateTfCard(bool retry_mount)
{
    if (!isTfCardInserted()) {
        unmountTfCard();
        _tf_card_info         = {};
        _tf_card_info.status  = TfCardStatus::NoCard;
        _tf_card_info.message = "NO TF CARD";
        return _tf_card_info;
    }

    if (_tf_mounted) {
        return _tf_card_info;
    }
    if (_tf_card_info.status == TfCardStatus::MountFailed && !retry_mount) {
        return _tf_card_info;
    }

    mountTfCard();
    return _tf_card_info;
}

const TfCardInfo& Hal::getTfCardInfo() const
{
    return _tf_card_info;
}

bool Hal::mountTfCard()
{
    _tf_card_info = {};

    SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);
    if (!SD.begin(PIN_CS, SPI)) {
        _tf_card_info.status  = TfCardStatus::MountFailed;
        _tf_card_info.message = "MOUNT FAILED";
        Serial.println("[TF] mount failed");
        return false;
    }

    const uint8_t card_type = SD.cardType();
    if (card_type == CARD_NONE) {
        SD.end();
        _tf_card_info.status  = TfCardStatus::MountFailed;
        _tf_card_info.message = "NO CARD";
        Serial.println("[TF] no card type");
        return false;
    }

    _tf_mounted                  = true;
    _tf_card_info.status         = TfCardStatus::Mounted;
    _tf_card_info.type           = card_type == CARD_MMC ? "MMC" : card_type == CARD_SD ? "SDSC" : "SDHC/SDXC";
    _tf_card_info.capacity_bytes = SD.cardSize();
    _tf_card_info.message        = "READY";
    Serial.printf("[TF] mounted: %s, %llu bytes\n", _tf_card_info.type.c_str(),
                  static_cast<unsigned long long>(_tf_card_info.capacity_bytes));
    return true;
}

void Hal::unmountTfCard()
{
    if (!_tf_mounted) {
        return;
    }
    SD.end();
    _tf_mounted = false;
    Serial.println("[TF] unmounted");
}

TfBenchmarkResult Hal::benchmarkTfCard()
{
    TfBenchmarkResult result;
    if (!_tf_mounted || _tf_card_info.status != TfCardStatus::Mounted) {
        result.message = "CARD NOT READY";
        return result;
    }

    auto* buffer = static_cast<uint8_t*>(malloc(TEST_BUFFER_SIZE));
    if (buffer == nullptr) {
        result.message = "NO MEMORY";
        return result;
    }
    for (size_t i = 0; i < TEST_BUFFER_SIZE; ++i) {
        buffer[i] = testPattern(i);
    }

    // Unique 8.3 name so existing user data is never overwritten.
    char path[32] = {};
    for (unsigned int attempt = 0; attempt < 4; ++attempt) {
        const unsigned long suffix = (static_cast<unsigned long>(millis()) + attempt) & 0xFFFFFUL;
        snprintf(path, sizeof(path), "/PDT%05lX.BIN", suffix);
        if (!SD.exists(path)) {
            break;
        }
        path[0] = '\0';
    }
    File file = path[0] != '\0' ? SD.open(path, FILE_WRITE) : File();
    if (!file) {
        result.message = "OPEN FAILED";
        free(buffer);
        return result;
    }

    const int64_t write_start = esp_timer_get_time();
    bool io_ok                = true;
    for (size_t offset = 0; offset < TEST_SIZE_BYTES; offset += TEST_BUFFER_SIZE) {
        if (file.write(buffer, TEST_BUFFER_SIZE) != TEST_BUFFER_SIZE) {
            io_ok = false;
            break;
        }
    }
    file.flush();
    const int64_t write_elapsed = esp_timer_get_time() - write_start;
    file.close();

    if (!io_ok) {
        result.message = "WRITE FAILED";
        SD.remove(path);
        free(buffer);
        return result;
    }

    File read_file = SD.open(path, FILE_READ);
    if (!read_file) {
        result.message = "READ OPEN FAILED";
        SD.remove(path);
        free(buffer);
        return result;
    }

    const int64_t read_start = esp_timer_get_time();
    for (size_t offset = 0; offset < TEST_SIZE_BYTES && io_ok; offset += TEST_BUFFER_SIZE) {
        if (read_file.read(buffer, TEST_BUFFER_SIZE) != static_cast<int>(TEST_BUFFER_SIZE)) {
            io_ok = false;
            break;
        }
        for (size_t i = 0; i < TEST_BUFFER_SIZE; ++i) {
            if (buffer[i] != testPattern(i)) {
                io_ok = false;
                break;
            }
        }
    }
    const int64_t read_elapsed = esp_timer_get_time() - read_start;
    read_file.close();
    SD.remove(path);
    free(buffer);

    if (!io_ok) {
        result.message = "READ VERIFY FAILED";
        return result;
    }

    result.success    = true;
    result.write_mb_s = throughputMbPerSecond(TEST_SIZE_BYTES, write_elapsed);
    result.read_mb_s  = throughputMbPerSecond(TEST_SIZE_BYTES, read_elapsed);
    result.message    = "OK";
    return result;
}

/* -------------------------------------------------------------------------- */
/*                               Display Flush                                */
/* -------------------------------------------------------------------------- */
void Hal::flushDisplay()
{
    if (_partial_refresh_request && !_full_redraw_request) {
        flushDisplayRegion(_refresh_x, _refresh_y, _refresh_width, _refresh_height, _partial_refresh_mode);
    } else {
        flushFullDisplay(_full_redraw_request ? _full_redraw_mode : display.getEpdMode());
    }
}

// Returns true when the region was escalated to a full-screen refresh
// (every PARTIAL_REFRESH_LIMIT partial updates, to clear ghosting).
bool Hal::flushDisplayRegion(int32_t x, int32_t y, int32_t width, int32_t height, m5gfx::epd_mode_t mode)
{
    ++_partial_refresh_count;
    if (_partial_refresh_count >= PARTIAL_REFRESH_LIMIT) {
        flushFullDisplay(m5gfx::epd_mode_t::epd_quality);
        return true;
    }
    display.setEpdMode(mode);
    display.display(x, y, width, height);
    return false;
}

void Hal::flushFullDisplay(m5gfx::epd_mode_t mode)
{
    display.setEpdMode(mode);
    display.display(0, 0, display.width(), display.height());
    // Callers hold DisplayGuard; keep it locked until the full waveform completes.
    display.waitDisplay();
    _partial_refresh_count = 0;
}

void Hal::requestRefresh()
{
    _refresh_request = true;
}

void Hal::requestRefresh(int32_t x, int32_t y, int32_t width, int32_t height, m5gfx::epd_mode_t mode)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    if (_partial_refresh_request) {
        const int32_t left   = std::min(_refresh_x, x);
        const int32_t top    = std::min(_refresh_y, y);
        const int32_t right  = std::max(_refresh_x + _refresh_width, x + width);
        const int32_t bottom = std::max(_refresh_y + _refresh_height, y + height);
        _refresh_x           = left;
        _refresh_y           = top;
        _refresh_width       = right - left;
        _refresh_height      = bottom - top;
        if (static_cast<int>(mode) < static_cast<int>(_partial_refresh_mode)) {
            _partial_refresh_mode = mode;
        }
    } else {
        _refresh_x               = x;
        _refresh_y               = y;
        _refresh_width           = width;
        _refresh_height          = height;
        _partial_refresh_mode    = mode;
        _partial_refresh_request = true;
    }
    requestRefresh();
}

void Hal::requestFullRedraw(m5gfx::epd_mode_t mode)
{
    _full_redraw_request = true;
    _full_redraw_mode    = mode;
    requestRefresh();
}

bool Hal::isRefreshRequested() const
{
    return _refresh_request;
}

bool Hal::isFullRedrawRequested() const
{
    return _full_redraw_request;
}

m5gfx::epd_mode_t Hal::partialRefreshMode() const
{
    return _partial_refresh_mode;
}

m5gfx::epd_mode_t Hal::fullRedrawMode() const
{
    return _full_redraw_mode;
}

void Hal::clearRefreshRequest()
{
    _refresh_request         = false;
    _full_redraw_request     = false;
    _partial_refresh_request = false;
    _partial_refresh_mode    = m5gfx::epd_mode_t::epd_text;
}
