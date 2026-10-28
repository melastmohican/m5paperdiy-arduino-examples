// Counter in M5GFX epd_fast mode, the reference for epdpi's epdpi_fast_mono example.
// Same layout and timing as the Rust example: "Count: N" at (40, 80), black on white,
// a second line naming the build, one update every 2 s, a full clear after every 10th update.
#include <M5GFX.h>
M5GFX display;

void setup(void)
{
  Serial.begin(115200);
  display.begin();
  if (display.width() < display.height())
  {
    display.setRotation(display.getRotation() ^ 1);
  }
  display.setEpdMode(epd_mode_t::epd_quality);
  display.clear(TFT_WHITE);
  display.waitDisplay();
  display.setEpdMode(epd_mode_t::epd_fast);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextSize(3);
}

void loop(void)
{
  static uint32_t count = 0;

  display.fillScreen(TFT_WHITE);
  display.setCursor(40, 80);
  display.printf("Count: %lu", (unsigned long)count);
  display.setCursor(40, 130);
  display.print("Arduino: M5GFX epd_fast");
  uint32_t start = millis();
  display.display();
  display.waitDisplay();
  Serial.printf("update %lu in %lu ms\n", (unsigned long)count, (unsigned long)(millis() - start));

  ++count;
  if (count % 10 == 0)
  {
    display.setEpdMode(epd_mode_t::epd_quality);
    display.clear(TFT_WHITE);
    display.waitDisplay();
    display.setEpdMode(epd_mode_t::epd_fast);
  }
  delay(2000);
}
