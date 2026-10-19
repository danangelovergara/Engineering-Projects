#include "deck_protocol.h"

#include "deck_render.h"

#include <esp_heap_caps.h>
#include <lvgl.h>
#include <lgfx.h>

#include <cstdlib>

namespace {

uint32_t parseColor(const String& value) {
  const char* digits = value.c_str();
  if (*digits == '#') ++digits;
  return static_cast<uint32_t>(strtoul(digits, nullptr, 16)) & 0xFFFFFF;
}

uint8_t hexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return 0;
}

String decodeField(const String& input) {
  String output;
  output.reserve(input.length());
  for (size_t index = 0; index < input.length(); ++index) {
    if (input[index] == '%' && index + 2 < input.length()) {
      const uint8_t value =
          (hexValue(input[index + 1]) << 4) | hexValue(input[index + 2]);
      output += static_cast<char>(value);
      index += 2;
    } else {
      output += input[index];
    }
  }
  return output;
}

uint8_t splitFields(const String& line, String* fields, uint8_t maximum) {
  uint8_t count = 0;
  int start = 0;
  while (count < maximum) {
    const int separator = line.indexOf('|', start);
    if (separator < 0) {
      fields[count++] = line.substring(start);
      break;
    }
    fields[count++] = line.substring(start, separator);
    start = separator + 1;
  }
  return count;
}

void finishImageTransfer(DeckState& state) {
  if (state.incomingImageIsIcon) {
    const uint8_t index = state.incomingImageButton - 1;
    if (state.buttonIcons[index] != nullptr) {
      heap_caps_free(state.buttonIcons[index]);
    }
    state.buttonIcons[index] = state.incomingImage;
    state.buttonIconLengths[index] = state.incomingImageLength;
  } else {
    if (state.backgroundImage != nullptr) {
      heap_caps_free(state.backgroundImage);
    }
    state.backgroundImage = state.incomingImage;
    state.backgroundImageLength = state.incomingImageLength;
  }
  state.incomingImage = nullptr;
  state.incomingImageLength = 0;
  state.incomingImageBytes = 0;
  state.incomingImageDiscardBytes = 0;
  state.incomingImageIsIcon = false;
  state.incomingImageButton = 0;
}

bool beginImageTransfer(DeckState& state, uint32_t length, bool icon,
                        uint8_t button) {
  if (state.incomingImage != nullptr) heap_caps_free(state.incomingImage);
  state.incomingImage = static_cast<uint8_t*>(
      heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (state.incomingImage == nullptr) {
    state.incomingImageLength = 0;
    state.incomingImageBytes = 0;
    state.incomingImageDiscardBytes = length;
    state.incomingImageStarted = millis();
    Serial.println(icon ? "ERROR|not enough memory for button icon"
                        : "ERROR|not enough memory for background image");
    return false;
  }
  state.incomingImageLength = length;
  state.incomingImageBytes = 0;
  state.incomingImageStarted = millis();
  state.incomingImageButton = button;
  state.incomingImageIsIcon = icon;
  return true;
}

void processCommand(DeckState& state, const String& line) {
  if (line == "HANDSHAKE") {
    sendReady();
    return;
  }
  if (line == "REDRAW") {
    drawDeck(state);
    Serial.println("SYNCED");
    return;
  }
  if (line.startsWith("ICONCLEAR|")) {
    const int buttonIndex = line.substring(10).toInt();
    if (buttonIndex < 1 || buttonIndex > kDeckButtonCount) {
      Serial.println("ERROR|button index must be between 1 and 15");
      return;
    }
    const uint8_t index = buttonIndex - 1;
    if (state.buttonIcons[index] != nullptr) {
      heap_caps_free(state.buttonIcons[index]);
      state.buttonIcons[index] = nullptr;
      state.buttonIconLengths[index] = 0;
    }
    return;
  }
  if (line.startsWith("BGJPG|")) {
    const uint32_t length = static_cast<uint32_t>(line.substring(6).toInt());
    if (length == 0 || length > kMaxBackgroundBytes) {
      Serial.println("ERROR|background image size is invalid");
      return;
    }
    beginImageTransfer(state, length, false, 0);
    return;
  }
  if (line.startsWith("ICONPNG|")) {
    String fields[3];
    const uint8_t fieldCount = splitFields(line, fields, 3);
    const int buttonIndex = fieldCount == 3 ? fields[1].toInt() : 0;
    const uint32_t length =
        fieldCount == 3 ? static_cast<uint32_t>(fields[2].toInt()) : 0;
    if (buttonIndex < 1 || buttonIndex > kDeckButtonCount || length == 0 ||
        length > 65536) {
      Serial.println("ERROR|button icon size or index is invalid");
      return;
    }
    beginImageTransfer(state, length, true, buttonIndex);
    return;
  }

  String fields[6];
  const uint8_t fieldCount = splitFields(line, fields, 6);
  if (fieldCount == 6 && fields[0] == "THEME") {
    if (fields[1] != "solid" && fields[1] != "gradient" &&
        fields[1] != "image") {
      Serial.println("ERROR|unknown background mode");
      return;
    }
    state.backgroundMode = fields[1];
    state.backgroundColor1 = parseColor(fields[2]);
    state.backgroundColor2 = parseColor(fields[3]);
    state.showBorders = fields[4] == "1";
    state.perButtonBackground = fields[5] == "1";
    return;
  }
  if (fieldCount == 5 && fields[0] == "BUTTON") {
    const int buttonIndex = fields[1].toInt();
    if (buttonIndex < 1 || buttonIndex > kDeckButtonCount) {
      Serial.println("ERROR|button index must be between 1 and 15");
      return;
    }
    DeckButton& button = state.buttons[buttonIndex - 1];
    button.color = parseColor(fields[2]);
    button.label = decodeField(fields[3]);
    button.icon = decodeField(fields[4]);
    return;
  }
  Serial.println("ERROR|unknown command");
}

}  // namespace

void sendReady() { Serial.println("READY|CROWPANEL|1.0.0"); }

bool imageTransferActive(const DeckState& state) {
  return state.incomingImage != nullptr || state.incomingImageDiscardBytes > 0;
}

void readSerialCommands(DeckState& state) {
  while (Serial.available() > 0) {
    if (state.incomingImageDiscardBytes > 0) {
      uint8_t discardBuffer[256];
      const uint32_t count = min(
          state.incomingImageDiscardBytes,
          min(static_cast<uint32_t>(Serial.available()),
              static_cast<uint32_t>(sizeof(discardBuffer))));
      const size_t received = Serial.readBytes(discardBuffer, count);
      state.incomingImageDiscardBytes -= received;
      continue;
    }
    if (state.incomingImage != nullptr &&
        state.incomingImageBytes < state.incomingImageLength) {
      const uint32_t remaining =
          state.incomingImageLength - state.incomingImageBytes;
      const int available = Serial.available();
      const uint32_t count = min(remaining, static_cast<uint32_t>(available));
      const size_t received =
          Serial.readBytes(state.incomingImage + state.incomingImageBytes, count);
      state.incomingImageBytes += received;
      if (state.incomingImageBytes == state.incomingImageLength) {
        finishImageTransfer(state);
      }
      continue;
    }

    const char character = static_cast<char>(Serial.read());
    if (character == '\r') continue;
    if (character == '\n') {
      state.commandBuffer[state.commandLength] = '\0';
      processCommand(state, String(state.commandBuffer));
      state.commandLength = 0;
    } else if (state.commandLength + 1 < sizeof(state.commandBuffer)) {
      state.commandBuffer[state.commandLength++] = character;
    } else {
      state.commandLength = 0;
      Serial.println("ERROR|command line is too long");
    }
  }
  if ((state.incomingImage != nullptr || state.incomingImageDiscardBytes > 0) &&
      millis() - state.incomingImageStarted > 30000) {
    if (state.incomingImage != nullptr) heap_caps_free(state.incomingImage);
    state.incomingImage = nullptr;
    state.incomingImageLength = 0;
    state.incomingImageBytes = 0;
    state.incomingImageDiscardBytes = 0;
    state.incomingImageButton = 0;
    state.incomingImageIsIcon = false;
    Serial.println("ERROR|image transfer timed out");
  }
}
