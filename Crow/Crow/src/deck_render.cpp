#include "deck_render.h"

#include <lvgl.h>
#include <lgfx.h>

#include <cstring>

namespace {

uint16_t color565(uint32_t color) {
  return lcd.color565((color >> 16) & 0xFF, (color >> 8) & 0xFF,
                      color & 0xFF);
}

uint32_t blendColor(uint32_t first, uint32_t second, uint32_t numerator,
                    uint32_t denominator) {
  const uint32_t red1 = (first >> 16) & 0xFF;
  const uint32_t green1 = (first >> 8) & 0xFF;
  const uint32_t blue1 = first & 0xFF;
  const uint32_t red2 = (second >> 16) & 0xFF;
  const uint32_t green2 = (second >> 8) & 0xFF;
  const uint32_t blue2 = second & 0xFF;
  const uint32_t red = static_cast<uint32_t>(
      static_cast<int32_t>(red1) +
      (static_cast<int32_t>(red2) - static_cast<int32_t>(red1)) * numerator /
          denominator);
  const uint32_t green = static_cast<uint32_t>(
      static_cast<int32_t>(green1) +
      (static_cast<int32_t>(green2) - static_cast<int32_t>(green1)) * numerator /
          denominator);
  const uint32_t blue = static_cast<uint32_t>(
      static_cast<int32_t>(blue1) +
      (static_cast<int32_t>(blue2) - static_cast<int32_t>(blue1)) * numerator /
          denominator);
  return (red << 16) | (green << 8) | blue;
}

void drawBackground(const DeckState& state) {
  if (state.backgroundMode == "image" && state.backgroundImage != nullptr &&
      state.backgroundImageLength > 0 &&
      lcd.drawJpg(state.backgroundImage, state.backgroundImageLength)) {
    return;
  }
  if (state.backgroundMode == "gradient") {
    constexpr uint16_t kBandHeight = 12;
    for (uint16_t y = 0; y < lcd.height(); y += kBandHeight) {
      const uint32_t color = blendColor(state.backgroundColor1,
                                        state.backgroundColor2, y,
                                        lcd.height() - 1);
      const int32_t bandHeight =
          lcd.height() - y < kBandHeight ? lcd.height() - y : kBandHeight;
      lcd.fillRect(0, y, lcd.width(), bandHeight, color565(color));
    }
  } else {
    lcd.fillScreen(color565(state.backgroundColor1));
  }
}

bool getSquarePngSize(const uint8_t* data, uint32_t length, uint32_t* size) {
  static constexpr uint8_t signature[] = {0x89, 'P', 'N', 'G',
                                          0x0D, 0x0A, 0x1A, 0x0A};
  if (data == nullptr || length < 24 ||
      memcmp(data, signature, sizeof(signature)) != 0 ||
      memcmp(data + 12, "IHDR", 4) != 0) {
    return false;
  }
  const uint32_t width = (static_cast<uint32_t>(data[16]) << 24) |
                         (static_cast<uint32_t>(data[17]) << 16) |
                         (static_cast<uint32_t>(data[18]) << 8) | data[19];
  const uint32_t height = (static_cast<uint32_t>(data[20]) << 24) |
                          (static_cast<uint32_t>(data[21]) << 16) |
                          (static_cast<uint32_t>(data[22]) << 8) | data[23];
  if (width != height || (width != 64 && width != 100)) return false;
  *size = width;
  return true;
}

}  // namespace

void drawDeck(const DeckState& state) {
  drawBackground(state);
  constexpr int16_t cellWidth = 160;
  constexpr int16_t cellHeight = 160;
  constexpr int16_t margin = 8;
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(textdatum_t::middle_center);
  for (uint8_t index = 0; index < kDeckButtonCount; ++index) {
    const uint8_t column = index % kColumnCount;
    const uint8_t row = index / kColumnCount;
    const int16_t x = column * cellWidth + margin;
    const int16_t y = row * cellHeight + margin;
    const int16_t width = cellWidth - margin * 2;
    const int16_t height = cellHeight - margin * 2;
    const DeckButton& button = state.buttons[index];
    if (state.perButtonBackground) {
      lcd.fillRoundRect(x, y, width, height, 12, color565(button.color));
    } else {
      lcd.fillRoundRect(x, y, width, height, 12, color565(0x171b22));
    }
    uint32_t sourceSize = 0;
    if (state.buttonIcons[index] != nullptr &&
        state.buttonIconLengths[index] > 0 &&
        getSquarePngSize(state.buttonIcons[index],
                         state.buttonIconLengths[index], &sourceSize)) {
      const float scale = static_cast<float>(width) / sourceSize;
      lcd.drawPng(state.buttonIcons[index], state.buttonIconLengths[index], x,
                  y, 0, 0, 0, 0, scale, scale);
    }
    if (state.showBorders) {
      lcd.drawRoundRect(x, y, width, height, 12, color565(button.color));
    }
  }
}
