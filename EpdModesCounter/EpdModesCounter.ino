// Counter in M5GFX epd_fast, epd_fastest and epd_text mode, five updates each, the reference for
// epdpi's epdpi_m5_modes example. A full epd_quality clear runs before each mode. One update every
// 2 s. Serial prints the time per update, so it can be compared with the Rust log.
#include <M5GFX.h>
M5GFX display;

static void run_mode(epd_mode_t mode, const char *name)
{
  display.setEpdMode(epd_mode_t::epd_quality);
  display.clear(TFT_WHITE);
  display.waitDisplay();
  display.setEpdMode(mode);
  uint32_t total = 0;
  for (uint32_t count = 0; count < 5; ++count)
  {
    display.fillScreen(TFT_WHITE);
    display.setCursor(40, 80);
    display.printf("Count: %lu", (unsigned long)count);
    display.setCursor(40, 130);
    display.printf("Arduino: M5GFX %s", name);
    uint32_t start = millis();
    display.display();
    display.waitDisplay();
    uint32_t ms = millis() - start;
    total += ms;
    Serial.printf("%s update %lu in %lu ms\n", name, (unsigned long)count, (unsigned long)ms);
    delay(2000);
  }
  Serial.printf("%s average %lu ms\n", name, (unsigned long)(total / 5));
}

void setup(void)
{
  Serial.begin(115200);
  display.begin();
  if (display.width() < display.height())
  {
    display.setRotation(display.getRotation() ^ 1);
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextSize(3);

  run_mode(epd_mode_t::epd_fast, "epd_fast");
  run_mode(epd_mode_t::epd_fastest, "epd_fastest");
  run_mode(epd_mode_t::epd_text, "epd_text");
  Serial.println("all modes done");
}

void loop(void)
{
  delay(1000);
}
