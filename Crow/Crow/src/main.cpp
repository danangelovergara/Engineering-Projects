#include <Arduino.h>
#include <lvgl.h>
#include <lgfx.h>

#include "deck_protocol.h"
#include "deck_render.h"
#include "deck_state.h"
#include "deck_touch.h"

namespace {

constexpr uint32_t kSerialBaud = 115200;
DeckState deck;

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  lcd.setup();
  for (uint8_t index = 0; index < kDeckButtonCount; ++index) {
    deck.buttons[index].label = String(index + 1);
    deck.buttons[index].color = 0x8b7cff;
  }
  drawDeck(deck);
  sendReady();
}

void loop() {
  readSerialCommands(deck);
  if (!imageTransferActive(deck)) updateTouch(deck);
  delay(8);
}
