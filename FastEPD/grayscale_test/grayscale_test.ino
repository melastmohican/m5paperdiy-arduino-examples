// 16-level grayscale test for the M5Stack PaperDIY (ED047TC1, 960x540) with FastEPD.
// Draws 16 swatches (0 = black ... 15 = white) with their index underneath.
//
//
// The upstream sketch set a custom gray matrix and a 1872x1404 panel size for a
// different panel (T5 Pro); both are dropped here. The panel's built-in matrix is used.
#include <FastEPD.h>
#include <Wire.h>
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


#define SWATCH_W 60  // 16 * 60 = 960
#define SWATCH_H 300

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Starting...");

  Wire.begin(41, 42, 100000);
  pm1EpdPower(true);
  int rc = epaper.initPanel(BB_PANEL_M5PAPERS3);
  if (rc != BBEP_SUCCESS) {
    Serial.printf("initPanel returned %d\n", rc);
    while (1) delay(1000);
  }
  epaper.setMode(BB_MODE_4BPP);
  epaper.fillScreen(0xf);
  for (int i = 0; i < 16; i++) {
    epaper.fillRect(i * SWATCH_W, 0, SWATCH_W, SWATCH_H, i);
  }
  epaper.drawRect(0, 0, 16 * SWATCH_W, SWATCH_H, 0);  // black outline around
  epaper.setFont(FONT_12x16);
  epaper.setTextColor(BBEP_BLACK);
  for (int i = 0; i < 16; i++) {
    epaper.setCursor(i * SWATCH_W + (i < 10 ? 24 : 18), SWATCH_H + 12);
    epaper.print(i, DEC);
  }
  epaper.fullUpdate(CLEAR_SLOW, false);
  Serial.println("Done");
  epaper.deInit();
  pm1EpdPower(false);
}

void loop() {
  delay(1000);
}
