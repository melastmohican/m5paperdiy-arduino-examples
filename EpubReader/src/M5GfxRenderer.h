#pragma once
#include <M5Unified.h>
#include "Epub/Renderer/Renderer.h"

// Renderer backed by an 8-bit grayscale M5Canvas that is pushed to the
// PaperDIY e-paper through M5GFX. Replaces the epdiy / M5EPD frame buffer
// renderers of the original project.
class M5GfxRenderer : public Renderer
{
  using epd = m5gfx::epd_mode_t;

  M5Canvas canvas;
  bool needs_gray_flush = false;

  const lgfx::IFont *get_font(bool bold, bool italic)
  {
    if (bold && italic) return &fonts::FreeSerifBoldItalic12pt7b;
    if (bold) return &fonts::FreeSerifBold12pt7b;
    if (italic) return &fonts::FreeSerifItalic12pt7b;
    return &fonts::FreeSerif12pt7b;
  }
  uint32_t gray(uint8_t v) { return canvas.color888(v, v, v); }

  void push(epd mode)
  {
    M5.Display.setEpdMode(mode);
    canvas.pushSprite(0, 0);
    M5.Display.waitDisplay();
  }

public:
  M5GfxRenderer() : canvas(&M5.Display)
  {
    canvas.setPsram(true);
    canvas.setColorDepth(lgfx::grayscale_8bit);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
    canvas.setTextDatum(top_left);
    canvas.setTextColor(gray(0), gray(255));
    clear_screen();
  }

  // ---- text -------------------------------------------------------------
  int get_text_width(const char *text, bool bold = false, bool italic = false) override
  {
    canvas.setFont(get_font(bold, italic));
    return canvas.textWidth(text);
  }
  void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false) override
  {
    canvas.setFont(get_font(bold, italic));
    canvas.setTextColor(gray(0));
    canvas.drawString(text, x + margin_left, y + margin_top);
  }

  // ---- shapes -----------------------------------------------------------
  void draw_pixel(int x, int y, uint8_t color) override
  {
    needs_gray(color);
    canvas.drawPixel(x + margin_left, y + margin_top, gray(color));
  }
  void draw_rect(int x, int y, int w, int h, uint8_t color = 0) override
  {
    needs_gray(color);
    canvas.drawRect(x + margin_left, y + margin_top, w, h, gray(color));
  }
  void fill_rect(int x, int y, int w, int h, uint8_t color = 0) override
  {
    needs_gray(color);
    canvas.fillRect(x + margin_left, y + margin_top, w, h, gray(color));
  }
  void draw_circle(int x, int y, int r, uint8_t color = 0) override
  {
    needs_gray(color);
    canvas.drawCircle(x, y, r, gray(color));
  }
  void fill_circle(int x, int y, int r, uint8_t color = 0) override
  {
    needs_gray(color);
    canvas.fillCircle(x, y, r, gray(color));
  }
  void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override
  {
    needs_gray(color);
    canvas.drawTriangle(x0, y0, x1, y1, x2, y2, gray(color));
  }
  void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) override
  {
    needs_gray(color);
    canvas.fillTriangle(x0, y0, x1, y1, x2, y2, gray(color));
  }

  // ---- images -----------------------------------------------------------
  bool get_image_size(const std::string &, const uint8_t *d, size_t n, int *w, int *h) override
  {
    if (n > 24 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G')
    {
      *w = (d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
      *h = (d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
      return *w > 0 && *h > 0;
    }
    if (n > 4 && d[0] == 0xFF && d[1] == 0xD8)
    {
      size_t i = 2;
      while (i + 9 < n)
      {
        if (d[i] != 0xFF) { i++; continue; }
        uint8_t m = d[i + 1];
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7) || m == 0xFF) { i += (m == 0xFF) ? 1 : 2; continue; }
        // SOFn markers carry the frame size (except DHT, JPG, DAC)
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
        {
          *h = (d[i + 5] << 8) | d[i + 6];
          *w = (d[i + 7] << 8) | d[i + 8];
          return *w > 0 && *h > 0;
        }
        i += 2 + ((d[i + 2] << 8) | d[i + 3]);
      }
    }
    return Renderer::get_image_size("", d, n, w, h);
  }
  void draw_image(const std::string &name, const uint8_t *d, size_t n, int x, int y, int w, int h) override
  {
    int iw = 0, ih = 0;
    bool ok = d && n && get_image_size(name, d, n, &iw, &ih) && iw > 0 && ih > 0;
    if (ok)
    {
      float scale = std::min((float)w / iw, (float)h / ih);
      bool png = n > 4 && d[0] == 0x89;
      canvas.setClipRect(x + margin_left, y + margin_top, w, h);
      ok = png ? canvas.drawPng(d, n, x + margin_left, y + margin_top, 0, 0, 0, 0, scale)
               : canvas.drawJpg(d, n, x + margin_left, y + margin_top, 0, 0, 0, 0, scale);
      canvas.clearClipRect();
      needs_gray_flush = true;
    }
    if (!ok) Renderer::draw_image(name, d, n, x, y, w, h);
  }

  // ---- screen -----------------------------------------------------------
  void needs_gray(uint8_t color) override
  {
    if (color != 0 && color != 255) needs_gray_flush = true;
  }
  bool has_gray() override { return needs_gray_flush; }
  void show_busy() override
  {
    const int w = 220, h = 70;
    int x = (canvas.width() - w) / 2, y = (canvas.height() - h) / 2;
    canvas.fillRect(x, y, w, h, gray(255));
    canvas.drawRect(x, y, w, h, gray(0));
    canvas.setFont(&fonts::FreeSerifBold12pt7b);
    canvas.setTextColor(gray(0));
    canvas.setTextDatum(middle_center);
    canvas.drawString("Loading...", x + w / 2, y + h / 2);
    canvas.setTextDatum(top_left);
    flush_area(x, y, w, h);
  }
  void show_img(int, int, int, int, const uint8_t *) override {}
  void clear_screen() override { canvas.fillSprite(gray(255)); }
  void flush_display() override
  {
    push(needs_gray_flush ? epd::epd_quality : epd::epd_text);
    needs_gray_flush = false;
  }
  void flush_area(int x, int y, int w, int h) override
  {
    M5.Display.setClipRect(x, y, w, h);
    push(epd::epd_fast);
    M5.Display.clearClipRect();
  }
  void reset() override
  {
    clear_screen();
    needs_gray_flush = false;
    push(epd::epd_quality);
  }

  int get_page_width() override { return canvas.width() - (margin_left + margin_right); }
  int get_page_height() override { return canvas.height() - (margin_top + margin_bottom); }
  int get_space_width() override { return get_text_width(" "); }
  int get_line_height() override
  {
    canvas.setFont(get_font(false, false));
    return canvas.fontHeight();
  }
};
