//
// Example to show a JPEG image
// as 16-gray levels
//
#include <FastEPD.h>
#include <Wire.h>
#include <JPEGDEC.h>
#include "mocha800x480.h"

// The image is 800x480; center it on the 960x540 panel. IMG_X must stay even
// (the draw callback packs two pixels per byte).
#define IMG_X 80
#define IMG_Y 30
JPEGDEC jpg;
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


int JPEGDraw(JPEGDRAW *pDraw)
{
  int x, y, iPitch = epaper.width()/2;
  uint8_t *s, *d, *pBuffer = epaper.currentBuffer();
  for (y=0; y<pDraw->iHeight; y++) {
    d = &pBuffer[((pDraw->y + y)*iPitch) + (pDraw->x/2)];
    s = (uint8_t *)pDraw->pPixels;
    s += (y * pDraw->iWidth);
    for (x=0; x<pDraw->iWidth; x+=2) {
        *d++ = (s[0] & 0xf0) | (s[1] >> 4);
        s += 2;
    } // for x
  } // for y
  return 1;
} /* JPEGDraw() */

void setup() {
  Serial.begin(115200);
  delay(3000); // wait for CDC-Serial to start
  // M5Stack PaperDIY (960x540 ED047TC1), FastEPD main branch.
  Wire.begin(41, 42, 100000);
  pm1EpdPower(true);
  if (epaper.initPanel(BB_PANEL_M5PAPERS3) != BBEP_SUCCESS) {
    Serial.println("initPanel failed");
    return;
  }
  epaper.setMode(BB_MODE_4BPP);
  epaper.fillScreen(0xf);
  if (jpg.openFLASH((uint8_t *)mocha800x480, sizeof(mocha800x480), JPEGDraw)) {
      jpg.setPixelType(EIGHT_BIT_GRAYSCALE);
      jpg.decode(IMG_X, IMG_Y, 0);
      jpg.close();
      epaper.fullUpdate(CLEAR_SLOW, false);
      Serial.println("Done");
      epaper.deInit();
      pm1EpdPower(false);
  } else {
      Serial.println("JPEG open failed");
  }
} /* setup() */

void loop() {
  delay(50000);
 // esp_sleep_enable_timer_wakeup(5000000);
 // esp_deep_sleep_start();

} /* loop() */
