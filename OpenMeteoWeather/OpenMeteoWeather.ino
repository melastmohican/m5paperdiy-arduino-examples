// Open-Meteo weather dashboard for the M5Stack PaperDIY (ED047TC1, 960x540, 16 grays), drawn with M5GFX.
//
// Ported from the LilyGo T5 4.7" version in
// https://github.com/melastmohican/lilygot5-47-s3-arduino-examples, which in turn comes from the
// Seeed reTerminal E1002 "WeatherStation" sketch (GxEPD2, 800x480 6-color).
// Weather data comes from Open-Meteo (https://open-meteo.com), which needs no API key.
// Weather icons are Lucide icons (ISC License, see icons/LICENSE-lucide.txt).
// Battery voltage is read from the M5PM1 power chip (I2C 0x6E, registers 0x22/0x23).
//
// Setup: copy secret.h.example to secret.h (WiFi), then set the location below.
//
// With a USB host connected the sketch stays awake and refreshes every DevRefreshMinutes;
// otherwise it deep-sleeps SleepMinutes between refreshes.

//#define DEV_NO_SLEEP
#define DevRefreshMinutes 5
#define SleepMinutes      60

#include <M5Unified.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

#include <M5PM1.h>
#include "secret.h"       // ssid, password (gitignored)

#include "icons/icon_s_clear_sky.h"
#include "icons/icon_s_fog.h"
#include "icons/icon_s_gray.h"
#include "icons/icon_s_partly_cloudy.h"
#include "icons/icon_s_rain.h"
#include "icons/icon_s_snow.h"
#include "icons/icon_s_storm.h"
#include "icons/icon_s_full_battery.h"
#include "icons/icon_s_half_battery.h"
#include "icons/icon_s_high_battery.h"
#include "icons/icon_s_low_battery.h"

// ===== User and location =====
String myUsername = "melastmohican";
String myCity     = "Bellevue";
float  latitude   = 47.6101;
float  longitude  = -122.2015;
bool   metricUnits = true;                                // false = Fahrenheit / mph / inch
const char *timezone = "PST8PDT,M3.2.0,M11.1.0";          // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char *ntpServer = "0.north-america.pool.ntp.org";

// ===== Gray levels (drawn as 8-bit values, M5GFX converts to the 16-gray panel) =====
#define White      0xFF
#define LightGrey  0xBB
#define Grey       0x88
#define DarkGrey   0x44
#define Black      0x00
#define PanelGray8 0xDD   // info panel fill

enum alignment { LEFT, RIGHT, CENTER };

// ===== Layout (960 x 540) =====
static const int SCREEN_W = 960;
static const int SCREEN_H = 540;
static const int HEADER_H = 130;
static const int USER_X   = 680;                 // light-gray info panel (battery, date, user)
static const int GRAPH_Y  = HEADER_H;
static const int GRAPH_H  = 230;
static const int WEEK_Y   = GRAPH_Y + GRAPH_H;   // 360
static const int WEEK_H   = SCREEN_H - WEEK_Y;   // 180
static const int ICON     = 48;                  // bitmaps are 48x48, 1 bit per pixel

struct WeatherData {
  float currentTemp, feelsLike;
  int   weatherCode;
  String weatherDescription;
  float dailyMinTemp[7], dailyMaxTemp[7];
  int   dailyWeatherCodes[7];
  String dailyNames[7];
  float hourlyTemps[6], hourlyPrecip[6];
  String hourlyTimes[6];
  String lastUpdatedDate, lastUpdatedTime;
  int   currentHour;
  String tempLabel;
};

M5GFX &display = M5.Display;
WeatherData wx;
double batteryVolts = 0;

// ---------------------------------------------------------------------------------------------
// Drawing helpers (M5GFX)
// ---------------------------------------------------------------------------------------------
static inline uint32_t gray(uint8_t v) { return display.color888(v, v, v); }

// Fonts: the LilyGo version used Open Sans bitmaps; these are the closest built-in sizes.
#define FONT8   (&fonts::FreeSans9pt7b)
#define FONT10  (&fonts::FreeSansBold9pt7b)
#define FONT12  (&fonts::FreeSansBold12pt7b)
#define FONT18  (&fonts::FreeSansBold18pt7b)
#define FONT24  (&fonts::FreeSansBold24pt7b)

void setFont(const lgfx::IFont *font) { display.setFont(font); }

// Text with its top edge at y.
void drawString(int x, int y, String text, alignment align) {
  display.setTextColor(gray(Black));
  display.setTextDatum(align == LEFT ? textdatum_t::top_left : align == RIGHT ? textdatum_t::top_right : textdatum_t::top_center);
  display.drawString(text, x, y);
}
// Same, with an opaque background (used on the gray info panel).
void drawStringColor(int x, int y, String text, alignment align, uint8_t fg, uint8_t bg) {
  display.setTextColor(gray(fg), gray(bg));
  display.setTextDatum(align == LEFT ? textdatum_t::top_left : align == RIGHT ? textdatum_t::top_right : textdatum_t::top_center);
  display.drawString(text, x, y);
}
void drawLine(int x0, int y0, int x1, int y1, uint8_t c) { display.drawLine(x0, y0, x1, y1, gray(c)); }
void drawRect(int x, int y, int w, int h, uint8_t c) { display.drawRect(x, y, w, h, gray(c)); }
void fillRect(int x, int y, int w, int h, uint8_t c) { display.fillRect(x, y, w, h, gray(c)); }
void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c) {
  display.fillTriangle(x0, y0, x1, y1, x2, y2, gray(c));
}

// 1-bit bitmap (MSB first, rows padded to bytes), scaled by an integer factor.
void drawBitmap(int x, int y, const uint8_t *bmp, int w, int h, uint8_t color, int scale = 1) {
  int stride = (w + 7) / 8;
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      if (pgm_read_byte(&bmp[j * stride + i / 8]) & (0x80 >> (i & 7)))
        display.fillRect(x + i * scale, y + j * scale, scale, scale, gray(color));
}

// ---------------------------------------------------------------------------------------------
// Weather code mapping (WMO codes used by Open-Meteo)
// ---------------------------------------------------------------------------------------------
const uint8_t *weatherIcon(int code) {
  switch (code) {
    case 0: return icon_s_clear_sky;
    case 1: case 2: case 3: return icon_s_partly_cloudy;
    case 45: case 48: return icon_s_fog;
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82: return icon_s_rain;
    case 71: case 73: case 75: case 77: case 85: case 86: return icon_s_snow;
    case 95: case 96: case 99: return icon_s_storm;
    default: return icon_s_gray;
  }
}

String weatherDescription(int code) {
  switch (code) {
    case 0: return "Clear sky";
    case 1: case 2: case 3: return "Mainly clear, partly cloudy";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: case 63: case 65: return "Rain";
    case 66: case 67: return "Freezing rain";
    case 71: case 73: case 75: return "Snowfall";
    case 77: return "Snow grains";
    case 80: case 81: case 82: return "Rain showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "Unknown condition";
  }
}

int batteryPercent(double v) {
  if (v >= 4.2) return 100;
  if (v <= 3.0) return 0;
  return (int)(((v - 3.0) / 1.2) * 100);
}

const uint8_t *batteryIcon(int pct) {
  if (pct >= 75) return icon_s_full_battery;
  if (pct >= 50) return icon_s_high_battery;
  if (pct >= 25) return icon_s_half_battery;
  return icon_s_low_battery;
}

// ---------------------------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------------------------
bool connectWiFi(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

bool setupTime() {
  configTzTime(timezone, ntpServer);
  struct tm t;
  return getLocalTime(&t, 10000);
}

String sunTime(const String &iso) {            // "2026-11-07T06:11" -> "06:11"
  int i = iso.indexOf('T');
  return (i != -1 && i + 5 < (int)iso.length()) ? iso.substring(i + 1, i + 6) : "??:??";
}

bool fetchWeather() {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) +
               "&longitude=" + String(longitude, 4) +
               "&daily=temperature_2m_max,temperature_2m_min,weather_code" +
               "&hourly=temperature_2m,precipitation_probability" +
               "&current=temperature_2m,apparent_temperature,weather_code" +
               "&timezone=auto";
  if (metricUnits) {
    wx.tempLabel = " C";
  } else {
    wx.tempLabel = " F";
    url += "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch";
  }

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Open-Meteo request failed: %d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("Open-Meteo JSON parse failed");
    return false;
  }

  struct tm t;
  getLocalTime(&t);
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  wx.lastUpdatedDate = buf;
  strftime(buf, sizeof(buf), "%H:%M", &t);
  wx.lastUpdatedTime = buf;
  wx.currentHour = t.tm_hour;

  wx.currentTemp = doc["current"]["temperature_2m"].as<float>();
  wx.feelsLike = doc["current"]["apparent_temperature"].as<float>();
  wx.weatherCode = doc["current"]["weather_code"].as<int>();
  wx.weatherDescription = weatherDescription(wx.weatherCode);

  static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (int i = 0; i < 7; i++) {
    wx.dailyMinTemp[i] = doc["daily"]["temperature_2m_min"][i].as<float>();
    wx.dailyMaxTemp[i] = doc["daily"]["temperature_2m_max"][i].as<float>();
    wx.dailyWeatherCodes[i] = doc["daily"]["weather_code"][i].as<int>();
    wx.dailyNames[i] = days[(t.tm_wday + i) % 7];
  }
  for (int i = 0; i < 6; i++) {
    int src = wx.currentHour + i;
    wx.hourlyTemps[i] = doc["hourly"]["temperature_2m"][src].as<float>();
    wx.hourlyPrecip[i] = doc["hourly"]["precipitation_probability"][src].as<float>();
    wx.hourlyTimes[i] = sunTime(doc["hourly"]["time"][src].as<String>());
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
void clearFrame() { display.fillScreen(gray(White)); }

void pushFrame() {
  display.display();
  display.waitDisplay();
}

void showMessage(const char *line1, const char *line2) {
  clearFrame();
  setFont(FONT24);
  drawString(60, 220, line1, LEFT);
  setFont(FONT18);
  drawString(60, 280, line2, LEFT);
  pushFrame();
}

void drawGraph() {
  const int graphX = 90, graphW = 780, marginX = 20, marginY = 20;
  const int graphY = GRAPH_Y + 35, graphH = GRAPH_H - 40;
  const int chartLeft = graphX + marginX;
  const int chartBottom = graphY + graphH - marginY;
  const int plotH = graphH - 2 * marginY;

  setFont(FONT10);
  drawString(10, GRAPH_Y + 6, "Hourly temperature and precipitation", LEFT);

  float tMin = 1000, tMax = -1000;
  for (int i = 0; i < 6; i++) {
    tMin = min(tMin, wx.hourlyTemps[i]);
    tMax = max(tMax, wx.hourlyTemps[i]);
  }
  float pMin = tMin - 2, pMax = tMax + 2, range = pMax - pMin;
  if (range == 0) range = 1;
  const float xStep = (graphW - 2 * marginX) / 5.0;

  drawLine(chartLeft, graphY + marginY, chartLeft, chartBottom, Black);
  drawLine(chartLeft, chartBottom, graphX + graphW + 20, chartBottom, Black);

  setFont(FONT8);
  drawString(chartLeft - 8, chartBottom - 12, String(pMin, 1) + wx.tempLabel, RIGHT);
  drawString(chartLeft - 8, chartBottom - plotH / 2 - 8, String((pMin + pMax) / 2, 1) + wx.tempLabel, RIGHT);
  drawString(chartLeft - 8, graphY + marginY - 2, String(pMax, 1) + wx.tempLabel, RIGHT);

  // Precipitation probability bars (0-100 %)
  for (int i = 0; i < 6; i++) {
    int x = chartLeft + i * xStep;
    int barH = (wx.hourlyPrecip[i] / 100.0) * plotH;
    int y = chartBottom - barH;
    if (barH > 10) fillRect(x + 5, y + 10, 14, barH - 10, LightGrey);
    drawString(x + 5, y - 16, String(wx.hourlyPrecip[i], 0) + "%", LEFT);
  }
  // Temperature line, two pixels thick
  for (int i = 0; i < 5; i++) {
    int x1 = chartLeft + i * xStep, x2 = chartLeft + (i + 1) * xStep;
    int y1 = chartBottom - ((wx.hourlyTemps[i] - pMin) / range) * plotH;
    int y2 = chartBottom - ((wx.hourlyTemps[i + 1] - pMin) / range) * plotH;
    drawLine(x1, y1, x2, y2, Black);
    drawLine(x1, y1 + 1, x2, y2 + 1, Black);
  }
  for (int i = 0; i < 6; i++)
    drawString(chartLeft + i * xStep + 3, chartBottom + 6, wx.hourlyTimes[i], LEFT);
}

void drawDashboard() {
  clearFrame();

  // Frames and light-gray info panel
  drawRect(0, 0, SCREEN_W, HEADER_H, Black);
  fillRect(USER_X + 1, 1, SCREEN_W - USER_X - 2, HEADER_H - 2, PanelGray8);
  drawLine(USER_X, 0, USER_X, HEADER_H, Black);
  drawRect(0, GRAPH_Y, SCREEN_W, GRAPH_H, Black);
  drawRect(0, WEEK_Y, SCREEN_W, WEEK_H, Black);

  // Section 1: city, temperature, condition. The icon is drawn 2x (96 px).
  drawBitmap(15, 17, weatherIcon(wx.weatherCode), ICON, ICON, Black, 2);
  setFont(FONT24);
  drawString(140, 12, myCity, LEFT);
  setFont(FONT18);
  drawString(140, 62, String(wx.currentTemp, 1) + wx.tempLabel, LEFT);
  setFont(FONT12);
  drawString(340, 70, wx.weatherDescription, LEFT);
  setFont(FONT8);
  drawString(340, 100, "Feels like " + String(wx.feelsLike, 1) + wx.tempLabel, LEFT);

  // Section 2: battery, date, user (black on light gray, anti-aliased against the gray via drawStringColor)
  int y = 12;
  setFont(FONT10);
  if (batteryVolts > 2.5) {
    int pct = batteryPercent(batteryVolts);
    drawBitmap(USER_X + 10, 4, batteryIcon(pct), ICON, ICON, Black);
    drawStringColor(USER_X + 65, 16, String(pct) + "%", LEFT, Black, PanelGray8);
  }
  drawStringColor(USER_X + 10, y + 52, wx.lastUpdatedDate, LEFT, Black, PanelGray8);
  drawStringColor(USER_X + 10, y + 74, "Last refresh: " + wx.lastUpdatedTime, LEFT, Black, PanelGray8);
  drawStringColor(USER_X + 10, y + 96, myUsername, LEFT, Black, PanelGray8);

  // Section 3: 6-day forecast
  const int dayW = SCREEN_W / 6;
  for (int i = 0; i < 6; i++) {
    int x = i * dayW;
    if (i > 0) drawLine(x, WEEK_Y + 10, x, SCREEN_H - 10, LightGrey);
    setFont(FONT12);
    drawString(x + dayW / 2, WEEK_Y + 10, wx.dailyNames[i], CENTER);
    drawBitmap(x + 14, WEEK_Y + 55, weatherIcon(wx.dailyWeatherCodes[i]), ICON, ICON, Black);

    setFont(FONT10);
    int ax = x + 85, ay = WEEK_Y + 62;
    fillTriangle(ax, ay, ax - 6, ay + 10, ax + 6, ay + 10, Black);           // max
    drawString(ax + 14, ay - 8, String(wx.dailyMaxTemp[i], 0) + wx.tempLabel, LEFT);
    ay += 40;
    fillTriangle(ax, ay + 10, ax - 6, ay, ax + 6, ay, Grey);                 // min
    drawString(ax + 14, ay - 8, String(wx.dailyMinTemp[i], 0) + wx.tempLabel, LEFT);
  }

  // Section 4: hourly graph
  drawGraph();
}

// ---------------------------------------------------------------------------------------------
// Power / main loop
// ---------------------------------------------------------------------------------------------
// Battery voltage from the M5PM1 power chip: VBAT_L (0x22) plus the low 4 bits of VBAT_H (0x23), in mV.
static M5PM1 pm1;
static bool pm1Ready = false;
double readBatteryVolts() {
  if (!pm1Ready) return 0;
  uint16_t mv = 0;
  if (pm1.readVbat(&mv) != M5PM1_OK) { Serial.println("PM1 readVbat failed"); return 0; }
  Serial.printf("Battery: %u mV\n", mv);
  return mv / 1000.0;
}

bool StayAwake() {
#ifdef DEV_NO_SLEEP
  return true;
#elif ARDUINO_USB_MODE == 1 && ARDUINO_USB_CDC_ON_BOOT
  return Serial.isConnected();
#else
  return false;
#endif
}

void BeginSleep() {
  display.powerSaveOn();   // panel supply off, the image stays
  if (StayAwake()) {
    Serial.println("USB connected: staying awake, next refresh in " + String(DevRefreshMinutes) + " min");
    return;
  }
  esp_sleep_enable_timer_wakeup((uint64_t)SleepMinutes * 60ULL * 1000000ULL);
  Serial.println("Deep sleep for " + String(SleepMinutes) + " min");
  Serial.flush();
  esp_deep_sleep_start();
}

void RunUpdate() {
  if (!connectWiFi(30000)) {
    Serial.println("WiFi failed");
    showMessage("WiFi connection failed.", "Check credentials or try again.");
  } else {
    setupTime();
    batteryVolts = readBatteryVolts();
    if (!fetchWeather()) {
      showMessage("HTTP request failed.", "Check location or try again.");
    } else {
      drawDashboard();
      pushFrame();
      Serial.println("Dashboard drawn");
    }
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  BeginSleep();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);   // USB-CDC: do not block forever without a monitor
  Serial.println("Open-Meteo weather, starting...");
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  pm1Ready = (pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K) == M5PM1_OK);
  if (!pm1Ready) Serial.println("M5PM1 init failed");
  if (display.width() < display.height()) display.setRotation(display.getRotation() ^ 1);
  display.setEpdMode(epd_mode_t::epd_quality);
  clearFrame();
  RunUpdate();
}

void loop() {   // only reached when BeginSleep() returned, i.e. USB connected
  delay(DevRefreshMinutes * 60UL * 1000UL);
  RunUpdate();
}
