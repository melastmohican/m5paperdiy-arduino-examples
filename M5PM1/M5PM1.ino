#include <M5Unified.h>
#include <M5PM1.h>

M5PM1 pm1;
bool pm1Ready = false;

static void drawMessage(const char* line1, const char* line2)
{
    M5.Display.clear();
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::FreeSansBold24pt7b);
    M5.Display.drawString(line1, M5.Display.width() / 2, M5.Display.height() / 2 - 30);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.drawString(line2, M5.Display.width() / 2, M5.Display.height() / 2 + 35);
}

static void drawTimerHome()
{
    M5.Display.clear();
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(&fonts::FreeSansBold24pt7b);
    M5.Display.drawString("M5PM1 TIMER", M5.Display.width() / 2, M5.Display.height() / 2 - 110);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.drawString("Btn A: power on after 3 seconds", M5.Display.width() / 2, M5.Display.height() / 2 - 35);
    M5.Display.drawString("Btn B: power off after 3 seconds", M5.Display.width() / 2, M5.Display.height() / 2 + 20);
    M5.Display.drawString("Press A or B to start the timer", M5.Display.width() / 2, M5.Display.height() / 2 + 75);
}

void setup()
{
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setEpdMode(epd_mode_t::epd_text);
    const m5pm1_err_t err =
        pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K);
    pm1Ready = (err == M5PM1_OK);

    if (!pm1Ready) {
        Serial.printf("M5PM1 init failed: %d\n", err);
        drawMessage("M5PM1 ERROR", "Check I2C");
        return;
    }

    Serial.println("BtnA: power on after 3s");
    Serial.println("BtnB: power off after 3s");
    drawTimerHome();
}

void loop()
{
    M5.update();
    if (!pm1Ready) {
        delay(1000);
        return;
    }

    if (M5.BtnA.wasPressed()) {
        drawMessage("Power OFF", "Power ON after 3s");
        delay(500);
        if (pm1.timerSet(3, M5PM1_TIM_ACTION_POWERON) == M5PM1_OK) {
            Serial.println("Power-on timer configured");
            pm1.shutdown();
        } else {
            Serial.println("Power-on timer configuration failed");
            drawMessage("Timer ERROR", "Power-on failed");
        }
    }

    if (M5.BtnB.wasPressed()) {
        drawMessage("Power OFF", "After 3 seconds");
        if (pm1.timerSet(3, M5PM1_TIM_ACTION_POWEROFF) == M5PM1_OK) {
            Serial.println("Power-off timer configured");
        } else {
            Serial.println("Power-off timer configuration failed");
            drawMessage("Timer ERROR", "Power-off failed");
        }
    }

    delay(20);
} 