// News: top headlines on the M5Stack PaperDIY (960x540, 16 grays), drawn with M5GFX.
//
// Ported from the LilyGo T5 4.7" version in
// https://github.com/melastmohican/lilygot5-47-s3-arduino-examples, which in turn comes from the
// Seeed reTerminal E1002 "News" sketch and the Soldered Inkplate example.
// Setup: copy secret.h.example to secret.h and set WiFi and a free NewsAPI key (https://newsapi.org/).
//
// With a USB host connected the sketch stays awake and refreshes every DevRefreshMinutes;
// otherwise it deep-sleeps SleepMinutes between refreshes. DEV_NO_SLEEP forces staying awake.

//#define DEV_NO_SLEEP
#define DevRefreshMinutes 30   // NewsAPI free tier allows 100 requests/day
#define SleepMinutes      60

#include <M5GFX.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secret.h"       // WIFI_SSID, WIFI_PASSWORD, NEWS_API_KEY (gitignored)

// ---------- CHANGE HERE ----------
const char *timezone = "PST8PDT,M3.2.0,M11.1.0";   // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char *newsCountry = "us";                      // NewsAPI country code
// ---------------------------------

#define MAX_ITEMS 4
struct NewsItem { String title; String description; };
NewsItem items[MAX_ITEMS];
int itemCount = 0;

M5GFX display;

// ===== Display helpers (M5GFX, gray levels) ===================================
#define White      0xFFFFFF
#define LightGrey  0xBBBBBB
#define DarkGrey   0x444444
#define Black      0x000000

static void setFont(const lgfx::IFont *font) { display.setFont(font); }
static void clearFrame() { display.fillScreen(White); }

// Draw text with its baseline at y.
static void drawText(int x, int y, const String &text) {
  display.setTextDatum(textdatum_t::baseline_left);
  display.drawString(text, x, y);
}

static int textWidth(const String &text) { return display.textWidth(text); }

static void drawTextCentered(int xCenter, int y, const String &text) {
  display.setTextDatum(textdatum_t::baseline_center);
  display.drawString(text, xCenter, y);
}

static void drawTextRight(int xRight, int y, const String &text) {
  display.setTextDatum(textdatum_t::baseline_right);
  display.drawString(text, xRight, y);
}

// The built-in GFX fonts are ASCII only: replace common UTF-8 punctuation, drop everything else non-ASCII.
static void asciiFold(String &s) {
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length();) {
    uint8_t c = s[i];
    if (c < 0x80) { out += (char)c; i++; continue; }
    if (c == 0xE2 && i + 2 < s.length()) {
      uint8_t b = s[i + 2];
      if (s[i + 1] == (char)0x80) {
        if (b == 0x98 || b == 0x99) out += '\'';
        else if (b == 0x9C || b == 0x9D) out += '"';
        else if (b == 0x93 || b == 0x94) out += '-';
        else if (b == 0xA6) out += "...";
      }
      i += 3;
      continue;
    }
    i += (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;   // skip the rest of a multi-byte sequence
  }
  s = out;
}

// Word-wrap text into a box of width w starting at baseline y with the current font.
// Draws at most maxLines lines ("..." on overflow). Returns the baseline for the next line.
static int drawWrapped(int x, int y, int w, int maxLines, const String &text, int lineHeight) {
  int line = 0, pos = 0, n = text.length();
  String cur;
  while (pos < n && line < maxLines) {
    int sp = text.indexOf(' ', pos);
    String word = (sp < 0) ? text.substring(pos) : text.substring(pos, sp);
    pos = (sp < 0) ? n : sp + 1;
    String trial = cur.length() ? cur + " " + word : word;
    if (textWidth(trial) > w && cur.length()) {
      drawText(x, y + line * lineHeight, cur);
      line++;
      cur = word;
    } else {
      cur = trial;
    }
  }
  if (line < maxLines && cur.length()) {
    drawText(x, y + line * lineHeight, cur);
    line++;
  } else if (pos < n && line >= maxLines) {
    drawText(x + w - 30, y + (maxLines - 1) * lineHeight, "...");
  }
  return y + line * lineHeight;
}

static void pushFrame() {
  display.display();
  display.waitDisplay();
}

static void drawMessage(const char *msg) {
  clearFrame();
  setFont(&fonts::FreeSans18pt7b);
  drawText(60, 280, msg);
  pushFrame();
}

static bool connectWiFi(unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

// ===== Power =================================================================
static bool StayAwake() {
#ifdef DEV_NO_SLEEP
  return true;
#elif ARDUINO_USB_MODE == 1 && ARDUINO_USB_CDC_ON_BOOT
  return Serial.isConnected();
#else
  return false;
#endif
}

static void BeginSleep() {
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

// ===== Network ================================================================
static bool fetchNews() {
  WiFiClientSecure client;
  client.setInsecure();   // public headlines only; no certificate pinning
  HTTPClient http;
  String url = String("https://newsapi.org/v2/top-headlines?country=") + newsCountry +
               "&pageSize=10&apiKey=" + NEWS_API_KEY;
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "M5Stack-PaperDIY");
  http.useHTTP10(true);   // no chunked encoding, so the JSON stream parses cleanly
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[news] HTTP %d\n", code);
    http.end();
    return false;
  }
  JsonDocument filter;   // keep only what we draw to save RAM
  filter["articles"][0]["title"] = true;
  filter["articles"][0]["description"] = true;
  JsonDocument doc;
  String body = http.getString();   // read it all first; parsing the live stream hit IncompleteInput
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[news] JSON error: %s\n", err.c_str());
    return false;
  }
  itemCount = 0;
  for (JsonObject a : doc["articles"].as<JsonArray>()) {
    if (itemCount >= MAX_ITEMS) break;
    const char *t = a["title"];
    const char *d = a["description"];
    if (!t || !d || !*d) continue;   // skip articles without a description
    items[itemCount].title = t;
    items[itemCount].description = d;
    asciiFold(items[itemCount].title);
    asciiFold(items[itemCount].description);
    itemCount++;
  }
  return itemCount > 0;
}

// ===== Drawing ================================================================
static void drawNews() {
  const int W = display.width(), margin = 40;

  struct tm t;
  getLocalTime(&t, 5000);
  char dateStr[32], updStr[32];
  snprintf(dateStr, sizeof(dateStr), "Date: %02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  snprintf(updStr, sizeof(updStr), "Last update: %02d:%02d", t.tm_hour, t.tm_min);

  clearFrame();
  display.setTextColor(Black, White);
  setFont(&fonts::FreeSerifBold24pt7b);
  drawTextCentered(W / 2, 48, "World News");
  display.fillRect(margin, 62, W - 2 * margin, 4, DarkGrey);

  setFont(&fonts::FreeSans9pt7b);
  drawText(margin, 92, dateStr);
  drawTextRight(W - margin, 92, updStr);
  display.fillRect(margin, 102, W - 2 * margin, 2, Black);

  const int top = 112, boxH = 105;
  for (int i = 0; i < itemCount; i++) {
    int y = top + i * boxH;
    setFont(&fonts::FreeSerifBold12pt7b);
    int next = drawWrapped(margin, y + 24, W - 2 * margin, 2, items[i].title, 28);
    setFont(&fonts::FreeSerif9pt7b);
    drawWrapped(margin, next + 4, W - 2 * margin, 2, items[i].description, 22);
    if (i < itemCount - 1) display.drawFastHLine(margin, y + boxH - 6, W - 2 * margin, LightGrey);
  }
  pushFrame();
}

// ===== Main ===================================================================
static void RunUpdate() {
  if (!connectWiFi(30000)) {
    Serial.println("WiFi failed");
    drawMessage("WiFi connection failed.");
  } else {
    configTzTime(timezone, "0.north-america.pool.ntp.org");
    struct tm t;
    getLocalTime(&t, 10000);
    bool ok = fetchNews();
    Serial.printf("fetch %s, %d items\n", ok ? "ok" : "FAILED", itemCount);
    if (ok) drawNews();
    else drawMessage("Failed to fetch news.");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  BeginSleep();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);   // USB-CDC: do not block forever without a monitor
  Serial.println("News, starting...");
  display.begin();
  if (display.width() < display.height()) display.setRotation(display.getRotation() ^ 1);
  display.setEpdMode(epd_mode_t::epd_quality);
  display.setTextColor(Black, White);
  clearFrame();
  RunUpdate();
}

void loop() {   // only reached when BeginSleep() returned, i.e. USB connected
  delay(DevRefreshMinutes * 60UL * 1000UL);
  RunUpdate();
}
