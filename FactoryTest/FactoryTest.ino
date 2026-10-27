// M5Stack PaperDIY factory test.
//
// Port of M5Paper_FactoryTest (M5EPD + EPDGUI touch frames) to M5Unified /
// M5GFX. PaperDIY has no touch panel, RTC or SHT30, so the touch UI is
// replaced by a two-button menu:
//
//   BtnA (GPIO4): move to the next item / run the action
//   BtnB (GPIO3): select / leave the current screen
//
// Screens: Factory Test (checklist), Compare (refresh modes), Gray Ramp,
// Life Game.
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

// TF card pins (same wiring as the PaperDIY demo).
static constexpr int SD_MISO = 40;
static constexpr int SD_MOSI = 38;
static constexpr int SD_SCLK = 39;
static constexpr int SD_CS   = 47;

struct Check {
    const char* name;
    bool pass;
    String detail;
};

M5Canvas canvas(&M5.Display);

using epd = m5gfx::epd_mode_t;

static int W, H;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void header(const char* title)
{
    canvas.fillSprite(TFT_WHITE);
    canvas.setTextColor(TFT_BLACK);
    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.drawString(title, 20, 16);
    canvas.drawFastHLine(0, 64, W, TFT_BLACK);
}

static void footer(const char* a, const char* b)
{
    canvas.setFont(&fonts::FreeSans12pt7b);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawFastHLine(0, H - 48, W, TFT_BLACK);
    canvas.setTextDatum(middle_left);
    canvas.drawString(a, 20, H - 24);
    canvas.setTextDatum(middle_right);
    canvas.drawString(b, W - 20, H - 24);
}

static void push(epd mode)
{
    M5.Display.setEpdMode(mode);
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
}

// 16 gray levels, 0 = black, 15 = white (matches the M5EPD 4bpp scale).
static uint32_t gray16(int level)
{
    uint8_t v = level * 17;
    return M5.Display.color888(v, v, v);
}

// ---------------------------------------------------------------------------
// Factory test (checklist)
// ---------------------------------------------------------------------------
static void drawChecklist(Check* items, int n, int passed)
{
    header("Factory Test");
    canvas.setFont(&fonts::FreeSans12pt7b);
    canvas.setTextDatum(top_right);
    char buf[24];
    snprintf(buf, sizeof(buf), "PASS %d/%d", passed, n);
    canvas.drawString(buf, W - 20, 24);

    const int rowH = 64;
    for (int i = 0; i < n; i++) {
        int y = 84 + i * rowH;
        canvas.setTextDatum(middle_left);
        String label = String(i + 1) + "." + items[i].name;
        if (items[i].pass) {
            canvas.drawString("[PASS] " + label, 20, y + rowH / 2);
        } else {
            // Failing items are drawn inverted, like ReversePartColor().
            canvas.fillRect(0, y, W, rowH - 4, TFT_BLACK);
            canvas.setTextColor(TFT_WHITE);
            canvas.drawString(label, 20, y + rowH / 2);
            canvas.setTextColor(TFT_BLACK);
        }
        canvas.setTextDatum(middle_right);
        canvas.setTextColor(items[i].pass ? TFT_BLACK : TFT_WHITE);
        canvas.drawString(items[i].detail, W - 20, y + rowH / 2);
        canvas.setTextColor(TFT_BLACK);
    }
    footer("A: rerun", "B: back");
}

static void runFactoryTest()
{
    Check items[] = {
        {"Battery", false, ""},
        {"Wi-Fi", false, ""},
        {"PSRAM", false, ""},
        {"SD Card", false, ""},
        {"Button A", false, "press A"},
        {"Button B", false, "press B"},
    };
    const int n = sizeof(items) / sizeof(items[0]);
    int passed  = 0;
    auto count  = [&]() {
        passed = 0;
        for (auto& c : items) passed += c.pass;
    };

    // Battery: the factory test passes once the reading changes; here a
    // plausible Li-ion voltage is enough.
    int32_t mv = M5.Power.getBatteryVoltage();
    items[0].pass   = mv > 3000 && mv < 4400;
    items[0].detail = String(mv / 1000.0f, 2) + " V";

    // Wi-Fi: original passed when the strongest AP was above -55 dBm.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int found = WiFi.scanNetworks();
    if (found > 0) {
        int32_t rssi    = WiFi.RSSI(0);
        items[1].pass   = rssi > -55;
        items[1].detail = WiFi.SSID(0) + " (" + String(rssi) + " dB)";
    } else {
        items[1].detail = "no AP";
    }
    WiFi.scanDelete();

    // PSRAM
    uint8_t* p = (uint8_t*)ps_malloc(16);
    if (p) {
        free(p);
        items[2].pass   = true;
        items[2].detail = "Free " + String(ESP.getFreePsram() / 1024.0f, 0) + " KiB";
    } else {
        items[2].detail = "[FAILED]";
    }

    // SD card
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
    if (SD.begin(SD_CS, SPI, 20000000) && SD.cardType() != CARD_NONE) {
        items[3].pass   = true;
        items[3].detail = String((uint32_t)(SD.cardSize() / (1024 * 1024))) + " MiB";
    } else {
        items[3].detail = "[FAILED]";
    }
    SD.end();

    count();
    drawChecklist(items, n, passed);
    push(epd::epd_quality);

    // Buttons: tapped A/B marks the item; "A: rerun" only after both pass so
    // that button presses can be tested before leaving.
    while (true) {
        M5.update();
        bool dirty = false;
        if (M5.BtnA.wasPressed() && !items[4].pass) {
            items[4].pass   = true;
            items[4].detail = "OK";
            dirty           = true;
        } else if (M5.BtnA.wasPressed()) {
            return runFactoryTest();
        }
        if (M5.BtnB.wasPressed()) {
            if (!items[5].pass) {
                items[5].pass   = true;
                items[5].detail = "OK";
                dirty           = true;
            } else {
                return;
            }
        }
        if (dirty) {
            count();
            drawChecklist(items, n, passed);
            push(epd::epd_text);
        }
        delay(20);
    }
}

// ---------------------------------------------------------------------------
// Compare: time each EPD refresh mode (Frame_Compare)
// ---------------------------------------------------------------------------
static void runCompare()
{
    struct Mode {
        epd mode;
        const char* name;
        const char* use;
    };
    static const Mode modes[] = {
        {epd::epd_quality, "quality", "High quality images"},
        {epd::epd_text, "text", "Text with white background"},
        {epd::epd_fast, "fast", "Graphics, fast flipping"},
        {epd::epd_fastest, "fastest", "Menus, anti-aliased text"},
    };
    const int n = sizeof(modes) / sizeof(modes[0]);
    uint32_t ms[n] = {};
    int sel        = 0;

    auto draw = [&]() {
        header("Compare");
        canvas.setFont(&fonts::FreeSans12pt7b);
        const int boxH = 120;
        for (int i = 0; i < n; i++) {
            int y = 84 + i * (boxH + 20);
            for (int g = 0; g < 16; g++) {
                canvas.fillRect(20 + g * ((W - 40) / 16), y, (W - 40) / 16, 40, gray16(g));
            }
            canvas.drawRect(20, y, W - 40, boxH, TFT_BLACK);
            if (i == sel) canvas.drawRect(22, y + 2, W - 44, boxH - 4, TFT_BLACK);
            canvas.setTextDatum(top_left);
            canvas.drawString(String(modes[i].name) + ": " + modes[i].use, 28, y + 52);
            if (ms[i]) {
                canvas.setTextDatum(bottom_right);
                canvas.drawString(String(ms[i]) + " ms", W - 28, y + boxH - 8);
            }
        }
        footer("A: next / B: refresh", "hold B: back");
    };

    draw();
    push(epd::epd_quality);
    while (true) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            sel = (sel + 1) % n;
            draw();
            push(epd::epd_fastest);
        }
        if (M5.BtnB.wasClicked()) {
            draw();
            uint32_t t = millis();
            push(modes[sel].mode);
            ms[sel] = millis() - t;
            draw();
            push(epd::epd_fastest);
        }
        if (M5.BtnB.wasHold()) return;
        delay(20);
    }
}

// ---------------------------------------------------------------------------
// Gray ramp: 16-level grayscale test (Screen_Test without the endless loop)
// ---------------------------------------------------------------------------
static void runGrayRamp()
{
    bool invert = false;
    while (true) {
        canvas.fillSprite(TFT_WHITE);
        const int bandH = H / 16;
        for (int i = 0; i < 16; i++) {
            canvas.fillRect(0, i * bandH, W, bandH, gray16(invert ? 15 - i : i));
        }
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.setTextDatum(middle_center);
        canvas.setTextColor(TFT_RED);  // converted to gray by the driver
        canvas.drawString(invert ? "15 -> 0" : "0 -> 15", W / 2, H / 2);
        canvas.setTextColor(TFT_BLACK);
        push(epd::epd_quality);

        while (true) {
            M5.update();
            if (M5.BtnA.wasPressed()) {
                invert = !invert;
                break;
            }
            if (M5.BtnB.wasPressed()) return;
            delay(20);
        }
    }
}

// ---------------------------------------------------------------------------
// Conway's Game of Life (Frame_Lifegame)
// ---------------------------------------------------------------------------
static void runLife()
{
    const int cell = 12;
    const int cols = W / cell;
    const int rows = (H - 112) / cell;
    auto* cur      = (uint8_t*)calloc(cols * rows, 1);
    auto* nxt      = (uint8_t*)calloc(cols * rows, 1);
    if (!cur || !nxt) {
        free(cur);
        free(nxt);
        return;
    }
    auto seed = [&]() {
        for (int i = 0; i < cols * rows; i++) cur[i] = (random(100) < 25);
    };
    seed();

    uint32_t gen = 0;
    bool paused  = false;
    while (true) {
        header("Life Game");
        canvas.setFont(&fonts::FreeSans12pt7b);
        canvas.setTextDatum(top_right);
        canvas.drawString("gen " + String(gen), W - 20, 24);
        const int oy = 72;
        for (int y = 0; y < rows; y++)
            for (int x = 0; x < cols; x++)
                if (cur[y * cols + x]) canvas.fillRect(x * cell + 1, oy + y * cell + 1, cell - 2, cell - 2, TFT_BLACK);
        footer(paused ? "A: resume / reseed" : "A: pause", "B: back");
        push(epd::epd_fast);

        // Step.
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                int nb = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (dx || dy) nb += cur[((y + dy + rows) % rows) * cols + (x + dx + cols) % cols];
                bool alive         = cur[y * cols + x];
                nxt[y * cols + x]  = alive ? (nb == 2 || nb == 3) : (nb == 3);
            }
        }
        memcpy(cur, nxt, cols * rows);
        gen++;

        uint32_t t = millis();
        while (millis() - t < 300 || paused) {
            M5.update();
            if (M5.BtnB.wasPressed()) {
                free(cur);
                free(nxt);
                return;
            }
            if (M5.BtnA.wasPressed()) {
                if (paused) {
                    seed();
                    gen = 0;
                }
                paused = !paused;
                break;
            }
            delay(20);
        }
    }
}

// ---------------------------------------------------------------------------
// Main menu (Frame_Main)
// ---------------------------------------------------------------------------
struct MenuItem {
    const char* name;
    void (*run)();
};

static const MenuItem menu[] = {
    {"Factory Test", runFactoryTest},
    {"Compare", runCompare},
    {"Gray Ramp", runGrayRamp},
    {"Life Game", runLife},
};
static constexpr int MENU_N = sizeof(menu) / sizeof(menu[0]);
static int menuSel          = 0;

static void drawMenu()
{
    header("PaperDIY Factory Test");
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    const int rowH = 110;
    for (int i = 0; i < MENU_N; i++) {
        int y = 100 + i * (rowH + 16);
        if (i == menuSel) {
            canvas.fillRoundRect(20, y, W - 40, rowH, 12, TFT_BLACK);
            canvas.setTextColor(TFT_WHITE);
        } else {
            canvas.drawRoundRect(20, y, W - 40, rowH, 12, TFT_BLACK);
            canvas.setTextColor(TFT_BLACK);
        }
        canvas.setTextDatum(middle_center);
        canvas.drawString(menu[i].name, W / 2, y + rowH / 2);
    }
    canvas.setTextColor(TFT_BLACK);
    footer("A: next", "B: open");
}

void setup()
{
    auto cfg           = M5.config();
    cfg.clear_display  = false;
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setRotation(0);
    W = M5.Display.width();
    H = M5.Display.height();
    Serial.printf("PaperDIY display %dx%d\n", W, H);

    canvas.setColorDepth(8);
    canvas.createSprite(W, H);
    drawMenu();
    push(epd::epd_quality);
}

void loop()
{
    M5.update();
    if (M5.BtnA.wasPressed()) {
        menuSel = (menuSel + 1) % MENU_N;
        drawMenu();
        push(epd::epd_fastest);
    }
    if (M5.BtnB.wasPressed()) {
        menu[menuSel].run();
        drawMenu();
        push(epd::epd_quality);
    }
    delay(20);
}
