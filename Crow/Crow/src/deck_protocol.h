#pragma once

#include "deck_state.h"

void sendReady();
void readSerialCommands(DeckState& state);
bool imageTransferActive(const DeckState& state);
