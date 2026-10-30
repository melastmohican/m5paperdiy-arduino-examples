//
// Example for animating images
//
#include <Wire.h>
#include <FastEPD.h>
#include "smiley.h"
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
// holds the position and direction of the objects
#define NUM_SMILEYS 8
int xArray[NUM_SMILEYS];
int yArray[NUM_SMILEYS];
int xDir[NUM_SMILEYS];
int yDir[NUM_SMILEYS];

void setup()
{
  int i, j;
  float f;

  Wire.begin(41, 42, 100000);
  pm1EpdPower(true);
  // The PaperDIY wires the ED047TC1 like the PaperS3 (same 8 data pins and control lines), and
  // BB_PANEL_M5PAPERS3 already carries the 960 x 540 size, so there is no setPanelSize() here.
  epaper.initPanel(BB_PANEL_M5PAPERS3);
  epaper.clearWhite(); // start with a white display (and buffer)
  epaper.setPasses(3); // fewer passes = faster updates
  // Create 8 random starting points
  for (i=0; i<NUM_SMILEYS; i++) {
    xArray[i] = random(4, epaper.width() - 104);
    yArray[i] = random(4, epaper.height() - 104);
    xDir[i] = random(0, 2) ? -4 : 4;
    yDir[i] = random(0, 2) ? -4 : 4;
  }
} /* setup() */

void loop()
{
int i, j;

  for (j=0; j<250; j++) { // 250 iterations
 //   epaper.fillScreen(BBEP_WHITE);
    for (i=0; i<NUM_SMILEYS; i++) { // draw current posiitions
        epaper.loadG5Image(smiley, xArray[i], yArray[i], BBEP_WHITE, BBEP_BLACK, 1.0f);
        // update the positions
        xArray[i] += xDir[i];
        if (xArray[i] < 4 || xArray[i] > epaper.width() - 104) xDir[i] = -xDir[i]; // bounce off X
        yArray[i] += yDir[i];
        if (yArray[i] < 4 || yArray[i] > epaper.height() - 104) yDir[i] = -yDir[i]; // bounce off Y
    } // for u;
    // Show on the display
    epaper.partialUpdate(true);
  } // for j
  delay(3000);
  epaper.clearWhite(false);
  epaper.deInit(); // save power by shutting down the TI power controller and I/O extender
  pm1EpdPower(false); // and the PaperDIY's panel supply
  while (1) {
    delay(1);
  }
} /* loop() */
