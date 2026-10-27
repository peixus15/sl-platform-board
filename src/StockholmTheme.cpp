#include "StockholmTheme.h"

namespace {

constexpr int kCharacterWidth = 6;
constexpr int kRowHeight = 16;
// Gap between the scrolling text and the departure time on its right.
constexpr int kTimeGap = 4;
// Blank space between the end of a scrolling text and its next repetition.
constexpr int kMarqueeGap = 3 * kCharacterWidth;
constexpr uint32_t kMarqueeStepMs = 40;    // One pixel per step: 25 px/s.
constexpr uint32_t kMarqueePauseMs = 2000;  // Hold the start of the text before each pass.

int textWidth(const String& text) {
  // Every glyph is 5 px plus 1 px spacing; the last glyph's spacing is not visible.
  return text.isEmpty() ? 0 : static_cast<int>(text.length()) * kCharacterWidth - 1;
}

}  // namespace

void StockholmTheme::begin(MatrixPanel_I2S_DMA& display) {
  display.cp437(true);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.fillScreen(0);
}

// Converts UTF-8 text to upper-case CP437, the encoding of the built-in GFX font,
// so Swedish letters (Å, Ä, Ö) are drawn as themselves.
String StockholmTheme::panelText(const String& text) const {
  String output;
  output.reserve(text.length());

  for (size_t index = 0; index < text.length(); ++index) {
    const uint8_t character = static_cast<uint8_t>(text[index]);
    if (character < 0x80) {
      output += static_cast<char>(toupper(character));
      continue;
    }

    if (character == 0xC3 && index + 1 < text.length()) {
      const uint8_t next = static_cast<uint8_t>(text[++index]);
      if (next == 0x85 || next == 0xA5) output += static_cast<char>(0x8F);       // Å å
      else if (next == 0x84 || next == 0xA4) output += static_cast<char>(0x8E);  // Ä ä
      else if (next == 0x96 || next == 0xB6) output += static_cast<char>(0x99);  // Ö ö
      else if (next == 0x89 || next == 0xA9) output += static_cast<char>(0x90);  // É é
      else if (next == 0x9C || next == 0xBC) output += static_cast<char>(0x9A);  // Ü ü
      continue;
    }

    // Skip the continuation bytes of any other multi-byte UTF-8 character.
    while (index + 1 < text.length() &&
           (static_cast<uint8_t>(text[index + 1]) & 0xC0) == 0x80) {
      ++index;
    }
  }

  return output;
}

// Prints CP437 text, then fixes the Swedish letters the GFX font draws awkwardly:
// - Ä and Ö have their dots on the glyph's top row, touching the letter; they are
//   lifted one pixel to leave a gap.
// - Å sits one pixel below the baseline of the other letters; the whole glyph is
//   lifted one pixel, which puts its ring on the same row as the lifted dots.
void StockholmTheme::drawText(Adafruit_GFX& target, int x, int y, const String& text,
                              uint16_t color) const {
  // Column bytes of CP437 0x8F (Å) from the GFX font, least significant bit on top.
  static constexpr uint8_t kRingA[5] = {0xF0, 0x28, 0x25, 0x28, 0xF0};

  target.setTextColor(color);
  target.setCursor(x, y);
  target.print(text);

  for (size_t index = 0; index < text.length(); ++index) {
    const uint8_t character = static_cast<uint8_t>(text[index]);
    const int left = x + static_cast<int>(index) * kCharacterWidth;
    if (character == 0x8E || character == 0x99) {  // Ä, Ö
      for (const int dotX : {left, left + 4}) {
        target.drawPixel(dotX, y, 0);
        target.drawPixel(dotX, y - 1, color);
      }
    } else if (character == 0x8F) {  // Å
      for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 8; ++row) target.drawPixel(left + column, y + row, 0);
        for (int row = 0; row < 8; ++row) {
          if (kRingA[column] & (1 << row)) target.drawPixel(left + column, y + row - 1, color);
        }
      }
    }
  }
}

// Draws text that fits as-is; text that is wider than `width` becomes a marquee.
void StockholmTheme::placeText(MatrixPanel_I2S_DMA& display, int x, int y, int width,
                               const String& text, uint8_t slot, uint32_t nowMs) {
  if (textWidth(text) <= width) {
    marqueeActive_[slot] = false;
    drawText(display, x, y, text, color_);
    return;
  }

  Marquee& marquee = marquees_[slot];
  // Keep the scroll position across data refreshes when the same text stays in place.
  const bool same = marqueeActive_[slot] && marquee.text == text && marquee.x == x &&
                    marquee.y == y && marquee.width == width;
  if (!same) {
    marquee.x = x;
    marquee.y = y;
    marquee.width = width;
    marquee.text = text;
    marquee.offset = 0;
    marquee.nextStepMs = nowMs + kMarqueePauseMs;
  }
  marqueeActive_[slot] = true;
  drawMarquee(display, marquee);
}

// Renders the visible slice into an off-screen 1-bit canvas, which clips it to the
// window, then copies the canvas over the window so nothing outside it is touched.
void StockholmTheme::drawMarquee(MatrixPanel_I2S_DMA& display, const Marquee& marquee) const {
  // One extra row on top for the lifted umlaut dots.
  GFXcanvas1 canvas(marquee.width, 9);
  canvas.cp437(true);
  canvas.setTextSize(1);
  canvas.setTextWrap(false);

  const int cycle = textWidth(marquee.text) + 1 + kMarqueeGap;
  drawText(canvas, -marquee.offset, 1, marquee.text, 1);
  drawText(canvas, cycle - marquee.offset, 1, marquee.text, 1);
  display.drawBitmap(marquee.x, marquee.y - 1, canvas.getBuffer(), marquee.width, 9, color_, 0);
}

void StockholmTheme::animate(MatrixPanel_I2S_DMA& display, uint32_t nowMs) {
  for (uint8_t slot = 0; slot < kMaxMarquees; ++slot) {
    if (!marqueeActive_[slot]) continue;
    Marquee& marquee = marquees_[slot];
    if (static_cast<int32_t>(nowMs - marquee.nextStepMs) < 0) continue;

    const int cycle = textWidth(marquee.text) + 1 + kMarqueeGap;
    marquee.offset = (marquee.offset + 1) % cycle;
    // After a full pass the start of the text is back in place: hold it there briefly.
    marquee.nextStepMs = nowMs + (marquee.offset == 0 ? kMarqueePauseMs : kMarqueeStepMs);
    drawMarquee(display, marquee);
  }
}

void StockholmTheme::render(MatrixPanel_I2S_DMA& display, const SlStopData& data,
                            uint8_t visibleRows, bool networkOk, uint8_t brightness) {
  const int panelWidth = display.width();
  const uint32_t nowMs = millis();
  color_ = display.color565(255, 142, 24);

  display.setBrightness8(brightness);
  display.fillScreen(0);
  display.setTextSize(1);
  display.setTextWrap(false);

  bool used[kMaxMarquees] = {false, false};
  const uint8_t rows = visibleRows > 2 ? 2 : visibleRows;
  if (data.departures.empty()) {
    drawText(display, 0, 4, panelText(networkOk ? "Väntar på tåg" : "Ingen kontakt med SL"),
             color_);
    if (rows > 1 && !data.stopName.isEmpty()) {
      placeText(display, 0, kRowHeight + 4, panelWidth, panelText(data.stopName), 1, nowMs);
      used[1] = true;
    }
  } else {
    for (uint8_t row = 0; row < rows; ++row) {
      const int y = row * kRowHeight + 4;
      if (!networkOk && row == 1) {
        drawText(display, 0, y, panelText("Ingen anslutning"), color_);
        continue;
      }
      if (row >= data.departures.size()) continue;

      // Line number on the left and time on the right stay fixed; the destination
      // between them scrolls when it is too long.
      const Departure& departure = data.departures[row];
      const String line = panelText(departure.line);
      const String time = panelText(departure.displayTime);
      const int timeX = panelWidth - static_cast<int>(time.length()) * kCharacterWidth;
      const int destinationX = (static_cast<int>(line.length()) + 1) * kCharacterWidth;

      drawText(display, 0, y, line, color_);
      drawText(display, timeX, y, time, color_);
      placeText(display, destinationX, y, timeX - kTimeGap - destinationX,
                panelText(departure.destination), row, nowMs);
      used[row] = true;
    }
  }

  // Forget marquees whose text did not need one this time (or whose row went away).
  for (uint8_t slot = 0; slot < kMaxMarquees; ++slot) {
    if (!used[slot]) marqueeActive_[slot] = false;
  }
}
