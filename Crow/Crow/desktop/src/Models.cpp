#include "crowdeck/Models.h"

namespace crowdeck {
namespace {

constexpr const wchar_t* kIcons[] = {L"♫", L"⌂", L"✉", L"⌕", L"⚡",
                                     L"▶", L"◉", L"⌘", L"●", L"✦",
                                     L"☾", L"＋", L"▶", L"⌂", L"✦"};
constexpr const wchar_t* kLabels[] = {
    L"Music",     L"Home",     L"Mail",   L"Search", L"Focus",
    L"Play",      L"Record",   L"Shortcuts", L"Mute",   L"Lights",
    L"Night",     L"New note", L"Next track", L"Desktop", L"Launch app"};
constexpr const wchar_t* kColors[] = {
    L"#8b7cff", L"#38bdf8", L"#4dd6a1", L"#4dd6a1", L"#f2a65a",
    L"#ff6b81", L"#ff6b81", L"#e879f9", L"#38bdf8", L"#f2a65a",
    L"#8b7cff", L"#4dd6a1", L"#38bdf8", L"#38bdf8", L"#e879f9"};

}  // namespace

Profile MakeDefaultProfile() {
  Profile profile;
  for (int index = 0; index < kButtonCount; ++index) {
    profile.buttons[index].icon = kIcons[index];
    profile.buttons[index].label = kLabels[index];
    profile.buttons[index].color = kColors[index];
  }
  return profile;
}

std::wstring ActionTypeName(ActionType type) {
  switch (type) {
    case ActionType::LaunchApplication:
      return L"Launch application";
    case ActionType::Keystrokes:
      return L"Send keystrokes";
    case ActionType::OpenUrl:
      return L"Open URL";
    case ActionType::RunScript:
      return L"Run script";
    default:
      return L"Do nothing";
  }
}

ActionType ParseActionType(const std::wstring& value) {
  if (value == L"launch_app") return ActionType::LaunchApplication;
  if (value == L"keystrokes") return ActionType::Keystrokes;
  if (value == L"open_url") return ActionType::OpenUrl;
  if (value == L"run_script") return ActionType::RunScript;
  return ActionType::DoNothing;
}

std::wstring BackgroundModeName(BackgroundMode mode) {
  switch (mode) {
    case BackgroundMode::Gradient:
      return L"gradient";
    case BackgroundMode::Image:
      return L"image";
    default:
      return L"solid";
  }
}

}  // namespace crowdeck
