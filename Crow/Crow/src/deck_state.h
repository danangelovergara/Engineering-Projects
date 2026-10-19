#pragma once

#include <Arduino.h>

constexpr uint8_t kDeckButtonCount = 15;
constexpr uint8_t kColumnCount = 5;
constexpr uint8_t kRowCount = 3;
constexpr uint32_t kMaxBackgroundBytes = 700000;

struct DeckButton {
  String label;
  String icon;
  uint32_t color = 0x8b7cff;
};

struct DeckState {
  DeckButton buttons[kDeckButtonCount];
  String backgroundMode = "solid";
  uint32_t backgroundColor1 = 0x101317;
  uint32_t backgroundColor2 = 0x25213f;
  bool showBorders = true;
  bool perButtonBackground = false;
  uint8_t pressedButton = 0;

  uint8_t* incomingImage = nullptr;
  uint32_t incomingImageLength = 0;
  uint32_t incomingImageBytes = 0;
  uint32_t incomingImageDiscardBytes = 0;
  uint32_t incomingImageStarted = 0;
  uint8_t incomingImageButton = 0;
  bool incomingImageIsIcon = false;

  uint8_t* buttonIcons[kDeckButtonCount] = {};
  uint32_t buttonIconLengths[kDeckButtonCount] = {};
  uint8_t* backgroundImage = nullptr;
  uint32_t backgroundImageLength = 0;

  char commandBuffer[512]{};
  size_t commandLength = 0;
};
