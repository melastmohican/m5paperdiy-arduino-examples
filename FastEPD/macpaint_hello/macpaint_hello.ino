//
// MacPaint "hello" handwriting animation with partial updates.
//
// Ported from the Good Display GDEQ0426T82 / XIAO MG24 sketch GDEQ0426T82_MacPaint
// (SSD1677, 800x480 monochrome), itself a port of upiir's
// https://github.com/upiir/seeed_eink_partial_update "hello" animation.
// Here: M5Stack PaperDIY (960x540) with FastEPD (main branch, see the README).
//
// The 640x480 MacPaint bitmap (image.h) is drawn once with a full refresh, then a brush follows the
// recorded pen path (positions.h) and only the pixels that changed are refreshed.
// It runs once and stops; press RST to replay.
//
#include <FastEPD.h>
#include <Wire.h>
#include "image.h"       // gImage_macos_paint_640x480, 1 bit per pixel, 1 = black
#include "positions.h"   // pen path in 800x480 coordinates

FASTEPD epaper;

// M5Stack PaperDIY: the panel supply is gated by GPIO2 of the PM1 power chip (I2C address 0x6E,
// SDA 41, SCL 42). FastEPD does not know about it, so it is switched here, as M5GFX does when it
// detects the board. Needs FastEPD from its `main` branch, the 2.2.0 release has no
// BB_PANEL_M5PAPERS3 pin map for this panel.
#define PM1_ADDR 0x6E
#define PM1_REG_GPIO_MODE 0x10
#define PM1_REG_GPIO_OUT 0x11
#define PM1_REG_GPIO_DRV 0x13
#define PM1_REG_GPIO_FUNC 0x16
#define PM1_EPD_PWR (1 << 2)

static void pm1Write(uint8_t reg, uint8_t value)
{
  Wire.beginTransmission(PM1_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

static void pm1Update(uint8_t reg, uint8_t clearBits, uint8_t setBits)
{
  Wire.beginTransmission(PM1_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false); // repeated start
  Wire.requestFrom((uint8_t)PM1_ADDR, (uint8_t)1);
  uint8_t cur = Wire.available() ? Wire.read() : 0;
  pm1Write(reg, (cur & ~clearBits) | setBits);
}

static void pm1EpdPower(bool on)
{
  if (on) {
    pm1Write(0x09, 0x00); // clear idle-sleep settings kept across a battery shutdown
    pm1Write(0x0A, 0x00);
    pm1Update(PM1_REG_GPIO_FUNC, 0x30, 0); // GPIO2 pin function: plain GPIO
    pm1Update(PM1_REG_GPIO_MODE, 0, PM1_EPD_PWR); // output
    pm1Update(PM1_REG_GPIO_DRV, PM1_EPD_PWR, 0); // push-pull
    pm1Update(PM1_REG_GPIO_OUT, 0, PM1_EPD_PWR); // high
    delay(10);
  } else {
    pm1Update(PM1_REG_GPIO_OUT, PM1_EPD_PWR, 0); // low
  }
}


// The original canvas is 800x480; center that frame on the 960x540 panel.
#define FRAME_X       80
#define FRAME_Y       30
// The 640x480 bitmap sat at x=80 inside the 800x480 frame.
#define IMG_X         (FRAME_X + 80)
#define IMG_Y         FRAME_Y
#define IMG_W         640
#define IMG_H         480
#define BRUSH_RADIUS  4

int animIndex = 0;

void drawBackground()
{
  epaper.fillScreen(BBEP_WHITE);
  for (int row = 0; row < IMG_H; row++) {
    for (int col = 0; col < IMG_W; col++) {
      uint8_t b = pgm_read_byte(&gImage_macos_paint_640x480[row * (IMG_W / 8) + (col / 8)]);
      if (b & (0x80 >> (col & 7))) epaper.drawPixel(IMG_X + col, IMG_Y + row, BBEP_BLACK);
    }
  }
  epaper.fullUpdate(CLEAR_SLOW, true);   // keeps the previous-frame state that partialUpdate() needs
}

void setup()
{
  Serial.begin(115200);
  Wire.begin(41, 42, 100000);
  pm1EpdPower(true);
  // The PaperDIY wires the ED047TC1 like the PaperS3, and BB_PANEL_M5PAPERS3 carries the 960 x 540 size.
  epaper.initPanel(BB_PANEL_M5PAPERS3);
  epaper.setPasses(5);                   // more passes = darker text, slower partial updates
  epaper.clearWhite();
  epaper.fullUpdate(CLEAR_NONE, true);   // required once before partialUpdate()
  drawBackground();
  Serial.println("Background drawn");
}

void loop()
{
  if (animIndex < NUM_POSITIONS) {
    int x = FRAME_X + (int16_t)pgm_read_word(&positions[animIndex][0]);
    int y = FRAME_Y + (int16_t)pgm_read_word(&positions[animIndex][1]);
    epaper.fillCircle(x, y, BRUSH_RADIUS, BBEP_BLACK);
    // Do not pass a row range here: FastEPD 2.2.0 leaves stale data in a DMA buffer for the skipped rows,
    // which draws gray vertical streaks. The previous/current diff already leaves unchanged pixels alone.
    epaper.partialUpdate(true);
    animIndex++;
    if (animIndex % 25 == 0) Serial.printf("%d / %d points\n", animIndex, NUM_POSITIONS);
  } else {
    Serial.println("Done, stopping");
    epaper.deInit();      // shut down the power controller and I/O expander; the image stays on the panel
    pm1EpdPower(false);   // and the PaperDIY's panel supply
    while (1) delay(1000);
  }
}
