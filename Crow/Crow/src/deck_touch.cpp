#include "deck_touch.h"

#include "deck_render.h"

#include <lvgl.h>
#include <lgfx.h>

extern int touch_last_x;
extern int touch_last_y;
bool touch_touched();

void updateTouch(DeckState& state) {
  const bool touched = touch_touched();
  if (touched) {
    const int16_t x = touch_last_x;
    const int16_t y = touch_last_y;
    uint8_t button = 0;
    if (x >= 0 && x < lcd.width() && y >= 0 && y < lcd.height()) {
      const uint8_t column = x * kColumnCount / lcd.width();
      const uint8_t row = y * kRowCount / lcd.height();
      button = row * kColumnCount + column + 1;
    }
    if (button != state.pressedButton) {
      if (state.pressedButton != 0) {
        Serial.printf("TOUCH|button=%u|released\n", state.pressedButton);
      }
      state.pressedButton = button;
      if (state.pressedButton != 0) {
        Serial.printf("TOUCH|button=%u|pressed\n", state.pressedButton);
      }
    }
  } else if (state.pressedButton != 0) {
    Serial.printf("TOUCH|button=%u|released\n", state.pressedButton);
    state.pressedButton = 0;
  }
}
