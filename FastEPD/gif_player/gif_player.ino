#include <FastEPD.h>
#include <Wire.h>
#include <AnimatedGIF.h>
#include "Roboto_Black_50.h"
#include "1bitsmallcity.h"
#include "spiral_1bit.h"
AnimatedGIF gif;
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

uint8_t *pFramebuffer;
int center_x, center_y;
// Variable that keeps count on how much screen has been partially updated
int n = 0;

//
// This doesn't have to be super efficient
//
void DrawPixel(int x, int y, uint8_t ucColor) {
  uint8_t ucMask;
  int index;

  x += center_x;
  y += center_y;
  ucMask = 0x80 >> (x & 7);
  index = (x >> 3) + (y * (epaper.width() / 8));
  if (ucColor)
    pFramebuffer[index] |= ucMask;  // black
  else
    pFramebuffer[index] &= ~ucMask;
}
//
// Callback function from the AnimatedGIF library
// It's passed each line as it's decoded
// Draw the line of pixels directly into the FastEPD framebuffer
//
void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  int x, y, iWidth;
  static uint8_t ucPalette[256];  // thresholded palette

  if (pDraw->y == 0) {  // first line, convert palette to 0/1
    for (x = 0; x < 256; x++) {
      uint16_t usColor = pDraw->pPalette[x];
      int gray = (usColor & 0xf800) >> 8;  // red
      gray += ((usColor & 0x7e0) >> 2);    // plus green*2
      gray += ((usColor & 0x1f) << 3);     // plus blue
      ucPalette[x] = (gray >> 9);          // 0->511 = 0, 512->1023 = 1
    }
  }
  y = pDraw->iY + pDraw->y;  // current line position within the GIF canvas
  iWidth = pDraw->iWidth;
  if (iWidth > epaper.width())
    iWidth = epaper.width();
  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {  // restore to background color
    for (x = 0; x < iWidth; x++) {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) {  // if transparency used
    uint8_t c, ucTransparent = pDraw->ucTransparent;
    int x;
    for (x = 0; x < iWidth; x++) {
      c = *s++;  // each source pixel is always 1 byte (even for 1-bit images)
      if (c != ucTransparent)
        DrawPixel(pDraw->iX + x, y, ucPalette[c]);
    }
  } else {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    for (x = 0; x < pDraw->iWidth; x++)
      DrawPixel(pDraw->iX + x, y, ucPalette[*s++]);
  }
  if (pDraw->y == pDraw->iHeight - 1)  // last line, render it to the display
    // Keep the power on. No row range: passing one leaves stale rows (gray streaks), see ../README.md.
    epaper.partialUpdate(true);
} /* GIFDraw() */

void setup() {
  BB_RECT rect;  // rectangle for getting the text size
  long l;
  int iFrame;
  Serial.begin(115200);
  Serial.println("Starting...");
  // M5Stack PaperDIY (960x540 ED047TC1).
  // initPanel() defaults to 1-bpp mode, which is what DrawPixel() writes into the
  // framebuffer directly. Do not call setMode(BB_MODE_4BPP) or setPanelSize() here.
  Wire.begin(41, 42, 100000);
  pm1EpdPower(true);
  // The PaperDIY wires the ED047TC1 like the PaperS3, and BB_PANEL_M5PAPERS3 carries the 960 x 540 size.
  epaper.initPanel(BB_PANEL_M5PAPERS3);
  gif.begin(LITTLE_ENDIAN_PIXELS);
  pFramebuffer = epaper.currentBuffer();  // we want to write directly into the framebuffer (faster)
  epaper.fillScreen(BBEP_WHITE);
  epaper.setFont(Roboto_Black_50);  // A compressed BB_FONT
  epaper.setTextColor(BBEP_BLACK);
  epaper.getStringBox("FastEPD GIF Demo", &rect);       // get the rectangle around the text
  epaper.setCursor((epaper.width() - rect.w) / 2, 90);  // center horizontally
  epaper.print("FastEPD GIF Demo");
  epaper.fullUpdate(CLEAR_SLOW, true);  // full update, power stays ON; required before partialUpdate()
  l = millis();
  iFrame = 0;
  //  if (gif.open((uint8_t *)spiral_1bit, sizeof(spiral_1bit), GIFDraw)) {
  if (gif.open((uint8_t *)_1bitsmallcity, sizeof(_1bitsmallcity), GIFDraw)) {
    center_x = (epaper.width() - gif.getCanvasWidth()) / 2;
    center_y = (epaper.height() - gif.getCanvasHeight()) / 2;
    //    Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    while (gif.playFrame(false, NULL)) {
      iFrame++;
    }  // play it as fast as possible
    gif.close();
  }
  l = millis() - l;
  Serial.printf("Played %d frames in %d ms\n", iFrame, (int)l);
  delay(3000);  // wait a few seconds before erasing the display
  epaper.clearBlack(true);
  epaper.clearWhite(true);
  epaper.einkPower(false);
  epaper.deInit();
  pm1EpdPower(false);
  while (1) {};  // we're done, sit here forever
} /* setup () */

void loop() {
} /* loop() */
