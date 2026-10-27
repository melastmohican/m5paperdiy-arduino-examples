#include <Arduino.h>
#include <M5Unified.h>

M5Canvas canvas(&M5.Display);

static void drawBlackBorder()
{
    const int screenW = M5.Display.width();
    const int screenH = M5.Display.height();

    canvas.drawRect(0, 0, screenW, screenH, TFT_BLACK);
    canvas.drawRect(2, 2, screenW - 4, screenH - 4, TFT_BLACK);
    canvas.drawRect(4, 4, screenW - 8, screenH - 8, TFT_BLACK);
}

static void drawMainDemo()
{
    const int screenW = M5.Display.width();
    const int screenH = M5.Display.height();

    static const uint16_t diyColors[4] = {
        TFT_BLACK,
        TFT_DARKGREY,
        TFT_LIGHTGREY,
        TFT_BLACK
    };

    canvas.fillSprite(TFT_WHITE);
    drawBlackBorder();
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_BLACK);

    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.drawString("PAPERDIY", screenW / 2, 115);

    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.drawString("Panel: AXE544127", screenW / 2, 205);

    char resolutionBuf[32];
    snprintf(resolutionBuf, sizeof(resolutionBuf), "Resolution: %d x %d", screenW, screenH);
    canvas.drawString(resolutionBuf, screenW / 2, 255);
    canvas.drawString("16-Level Grayscale E-Paper", screenW / 2, 305);

    const int barX = 120;
    const int barY = 370;
    const int barW = screenW - 240;
    const int barH = 58;
    const int cellW = barW / 4;

    for (int i = 0; i < 4; ++i) {
        const int x = barX + i * cellW;
        const int w = (i == 3) ? (barX + barW - x) : cellW;
        canvas.fillRect(x, barY, w, barH, diyColors[i]);
        canvas.drawRect(x, barY, w, barH, TFT_BLACK);
    }

    canvas.pushSprite(0, 0);
}

void setup()
{
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    M5.Display.setEpdMode(epd_mode_t::epd_quality);

    canvas.createSprite(M5.Display.width(), M5.Display.height());
    drawMainDemo();
}

void loop()
{
    M5.update();
    delay(100);
} 