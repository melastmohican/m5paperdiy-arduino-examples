#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>
#include <M5GFX.h>

static const char* const imagePaths[] = {
    "/PaperDIY_01.png",
    "/PaperDIY_02.png",
};
static constexpr size_t IMAGE_COUNT = sizeof(imagePaths) / sizeof(imagePaths[0]);

M5Canvas canvas(&M5.Display);
size_t currentImage = 0;
bool sdReady = false;

static void showStatus(const char* title, const char* detail)
{
    canvas.fillSprite(TFT_WHITE);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(TFT_BLACK);
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.drawString(title, M5.Display.width() / 2, M5.Display.height() / 2 - 35);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.drawString(detail, M5.Display.width() / 2, M5.Display.height() / 2 + 35);
    canvas.pushSprite(0, 0);
}

static void showImage(size_t index)
{
    currentImage = index % IMAGE_COUNT;
    const char* path = imagePaths[currentImage];

    if (!SD.exists(path)) {
        showStatus("PNG not found", path);
        Serial.printf("Image not found: %s\n", path);
        return;
    }

    M5.Display.setEpdMode(epd_mode_t::epd_quality);
    M5.Display.fillScreen(TFT_WHITE);
    if (!M5.Display.drawPngFile(SD, path)) {
        showStatus("PNG load failed", path);
        Serial.printf("Failed to draw PNG: %s\n", path);
        return;
    }

    Serial.printf("Displayed: %s\n", path);
}

void setup()
{
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setRotation(1); 
    M5.Display.setEpdMode(epd_mode_t::epd_text);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
    showStatus("PaperDIY microSD", "Initializing card");

    SPI.begin();
    if (!SD.begin()) {
        showStatus("microSD mount failed", "Check card / FAT32");
        Serial.println("microSD initialization failed");
        return;
    }

    sdReady = SD.cardType() != CARD_NONE;
    if (!sdReady) {
        showStatus("No microSD card", "Insert a FAT32 card");
        Serial.println("No microSD card detected");
        return;
    }

    Serial.printf("Card size: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

    File logFile = SD.open("/PaperDIY_Log.txt", FILE_WRITE);
    if (logFile) {
        logFile.println("PaperDIY microSD test");
        logFile.close();
        Serial.println("Wrote /PaperDIY_Log.txt");
    } else {
        Serial.println("Failed to open /PaperDIY_Log.txt for writing");
    }

    File readFile = SD.open("/PaperDIY_Log.txt", FILE_READ);
    if (readFile) {
        Serial.println("Contents of /PaperDIY_Log.txt:");
        while (readFile.available()) {
            Serial.write(readFile.read());
        }
        readFile.close();
    }

    showImage(currentImage);
}

void loop()
{
    M5.update();
    if (!sdReady) {
        delay(1000);
        return;
    }

    if (M5.BtnA.wasPressed()) {
        showImage((currentImage + IMAGE_COUNT - 1) % IMAGE_COUNT);
    }
    if (M5.BtnB.wasPressed()) {
        showImage(currentImage + 1);
    }

    delay(20);
} 