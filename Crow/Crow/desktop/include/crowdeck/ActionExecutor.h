#pragma once

#include "crowdeck/Models.h"

namespace crowdeck {

class ActionExecutor {
 public:
  static bool Execute(const ButtonConfig& button, std::wstring* error);
};

}  // namespace crowdeck
