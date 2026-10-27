#pragma once

#include "Departure.h"
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

class StockholmTheme {
 public:
  void begin(MatrixPanel_I2S_DMA& display);
  // Draws the full frame. Call only when the data or settings change.
  void render(MatrixPanel_I2S_DMA& display, const SlStopData& data, uint8_t visibleRows,
              bool networkOk, uint8_t brightness);
  // Advances any text that is too long to fit; redraws only those text windows.
  void animate(MatrixPanel_I2S_DMA& display, uint32_t nowMs);

 private:
  // A text window that scrolls right-to-left because its text does not fit.
  struct Marquee {
    int x = 0;
    int y = 0;
    int width = 0;
    String text;
    int offset = 0;
    uint32_t nextStepMs = 0;
  };

  String panelText(const String& text) const;
  void drawText(Adafruit_GFX& target, int x, int y, const String& text, uint16_t color) const;
  void placeText(MatrixPanel_I2S_DMA& display, int x, int y, int width, const String& text,
                 uint8_t slot, uint32_t nowMs);
  void drawMarquee(MatrixPanel_I2S_DMA& display, const Marquee& marquee) const;

  static constexpr uint8_t kMaxMarquees = 2;
  Marquee marquees_[kMaxMarquees];
  bool marqueeActive_[kMaxMarquees] = {false, false};
  uint16_t color_ = 0;
};
