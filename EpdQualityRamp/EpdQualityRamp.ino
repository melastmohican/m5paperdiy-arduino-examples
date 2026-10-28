// 16-level gray ramp in M5GFX epd_quality mode, the reference for epdpi's epdpi_gray_ramp.
// Sixteen bars 60 px wide and 480 px tall, a label line below, level 0 (black) on the left to level 15 (white) on the right.
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

  uint32_t start = millis();
  for (int level = 0; level < 16; ++level)
  {
    int v = level * 17;
    display.fillRect(level * 60, 0, 60, 480, display.color888(v, v, v));
  }
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.setTextSize(3);
  display.setCursor(40, 500);
  display.print("Arduino: M5GFX epd_quality");
  display.display();
  display.waitDisplay();
  Serial.printf("ramp done in %lu ms\n", (unsigned long)(millis() - start));
}

void loop(void)
{
  delay(1000);
}
