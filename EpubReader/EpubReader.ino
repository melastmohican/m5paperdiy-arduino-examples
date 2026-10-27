// EPUB reader for the M5Stack PaperDIY, ported from
// https://github.com/atomic14/diy-esp32-epub-reader (ESP-IDF + epdiy) to
// Arduino + M5Unified / M5GFX.
//
// Controls (the original has UP / DOWN / SELECT):
//   BtnA click : DOWN  (next item / next page)
//   BtnA hold  : UP    (previous item / previous page)
//   BtnB click : SELECT (open book / chapter, or back to the list)
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include "src/Epub/EpubList/Epub.h"
#include "src/Epub/EpubList/EpubList.h"
#include "src/Epub/EpubList/EpubReader.h"
#include "src/Epub/EpubList/EpubToc.h"
#include "src/M5GfxRenderer.h"

enum UIAction { NONE, UP, DOWN, SELECT };
enum UIState { SELECTING_EPUB, SELECTING_TABLE_CONTENTS, READING_EPUB };

// Parsing a book needs a deep stack.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

static constexpr int SD_MISO = 40;
static constexpr int SD_MOSI = 38;
static constexpr int SD_SCLK = 39;
static constexpr int SD_CS   = 47;

static UIState ui_state = SELECTING_EPUB;
static EpubListState epub_list_state;
static EpubTocState epub_index_state;

static M5GfxRenderer *renderer = nullptr;
static EpubList *epub_list     = nullptr;
static EpubReader *reader      = nullptr;
static EpubToc *contents       = nullptr;

static void handleEpubList(UIAction action, bool needs_redraw);

static void handleEpub(UIAction action)
{
  if (!reader)
  {
    reader = new EpubReader(epub_list_state.epub_list[epub_list_state.selected_item], renderer);
    reader->load();
  }
  switch (action)
  {
  case UP: reader->prev(); break;
  case DOWN: reader->next(); break;
  case SELECT:
    ui_state = SELECTING_EPUB;
    renderer->clear_screen();
    delete reader;
    reader = nullptr;
    if (!epub_list) epub_list = new EpubList(renderer, epub_list_state);
    handleEpubList(NONE, true);
    return;
  default: break;
  }
  reader->render();
}

static void handleEpubTableContents(UIAction action)
{
  if (!contents)
  {
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
    contents->set_needs_redraw();
    contents->load();
  }
  switch (action)
  {
  case UP: contents->prev(); break;
  case DOWN: contents->next(); break;
  case SELECT:
    ui_state = READING_EPUB;
    reader   = new EpubReader(epub_list_state.epub_list[epub_list_state.selected_item], renderer);
    reader->set_state_section(contents->get_selected_toc());
    reader->load();
    delete contents;
    contents = nullptr;
    handleEpub(NONE);
    return;
  default: break;
  }
  contents->render();
}

static void handleEpubList(UIAction action, bool needs_redraw)
{
  if (!epub_list)
  {
    epub_list = new EpubList(renderer, epub_list_state);
    if (epub_list->load("/fs/")) Serial.println("Epub files loaded");
  }
  if (needs_redraw) epub_list->set_needs_redraw();
  switch (action)
  {
  case UP: epub_list->prev(); break;
  case DOWN: epub_list->next(); break;
  case SELECT:
    ui_state = SELECTING_TABLE_CONTENTS;
    contents = new EpubToc(epub_list_state.epub_list[epub_list_state.selected_item], epub_index_state, renderer);
    contents->load();
    contents->set_needs_redraw();
    handleEpubTableContents(NONE);
    return;
  default: break;
  }
  epub_list->render();
}

static void handleUserInteraction(UIAction action, bool needs_redraw)
{
  switch (ui_state)
  {
  case READING_EPUB: handleEpub(action); break;
  case SELECTING_TABLE_CONTENTS: handleEpubTableContents(action); break;
  default: handleEpubList(action, needs_redraw); break;
  }
}

static void draw_battery_level(int percentage)
{
  renderer->set_margin_top(0);
  const int width = 40, height = 20, margin_right = 5, margin_top = 10;
  int xpos          = renderer->get_page_width() - width - margin_right;
  int percent_width = width * percentage / 100;
  renderer->fill_rect(xpos, margin_top, width, height, 255);
  renderer->fill_rect(xpos + width - percent_width, margin_top, percent_width, height, 0);
  renderer->draw_rect(xpos, margin_top, width, height, 0);
  renderer->fill_rect(xpos - 4, margin_top + height / 4, 4, height / 2, 0);
  renderer->set_margin_top(35);
}

void setup()
{
  auto cfg          = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(0);

  renderer = new M5GfxRenderer();
  renderer->set_margin_top(35);
  renderer->set_margin_left(10);
  renderer->set_margin_right(10);

  // Mount the card at /fs so the original fopen("/fs/...") paths keep working.
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 20000000, "/fs"))
    Serial.println("SD mount failed");

  renderer->reset();
  handleUserInteraction(NONE, true);
  draw_battery_level(M5.Power.getBatteryLevel());
  renderer->flush_display();
}

void loop()
{
  M5.update();
  UIAction action = NONE;
  if (M5.BtnA.wasClicked()) action = DOWN;
  else if (M5.BtnA.wasHold()) action = UP;
  else if (M5.BtnB.wasClicked()) action = SELECT;

  if (action != NONE)
  {
    handleUserInteraction(action, false);
    draw_battery_level(M5.Power.getBatteryLevel());
    renderer->flush_display();
  }
  delay(20);
}
