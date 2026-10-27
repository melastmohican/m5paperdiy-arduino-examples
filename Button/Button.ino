#include <M5Unified.h>

M5Canvas canvas(&M5.Display);
const char* lastAction = "Press BtnA or BtnB";

void drawStatus()
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();

    canvas.fillSprite(TFT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.drawString("PaperDIY Button Test", w / 2, h / 2 - 100);

    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.drawString("Last action:", w / 2, h / 2 - 20);
    canvas.drawString(lastAction, w / 2, h / 2 + 50);
    canvas.drawString("BtnA: GPIO4   BtnB: GPIO3", w / 2, h / 2 + 135);
    canvas.pushSprite(0, 0);
}

void setup()
{
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    M5.Display.setEpdMode(epd_mode_t::epd_text);

    canvas.createSprite(M5.Display.width(), M5.Display.height());
    drawStatus();
}

void loop()
{
    M5.update();

    if (M5.BtnA.wasPressed()) {
        lastAction = "BtnA pressed";
        drawStatus();
    }
    if (M5.BtnB.wasPressed()) {
        lastAction = "BtnB pressed";
        drawStatus();
    }

    delay(20);
} 