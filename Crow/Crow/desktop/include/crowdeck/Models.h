#pragma once

#include <array>
#include <string>
#include <vector>

namespace crowdeck {

constexpr int kButtonCount = 15;

enum class ActionType {
  DoNothing,
  LaunchApplication,
  Keystrokes,
  OpenUrl,
  RunScript,
};

struct ButtonAction {
  ActionType type = ActionType::DoNothing;
  std::wstring value;
};

struct ButtonConfig {
  std::wstring icon = L"✦";
  std::wstring iconPath;
  std::wstring label;
  std::wstring color = L"#8b7cff";
  ButtonAction action;
};

enum class BackgroundMode {
  Solid,
  Gradient,
  Image,
};

struct ThemeConfig {
  BackgroundMode mode = BackgroundMode::Solid;
  std::wstring color1 = L"#101317";
  std::wstring color2 = L"#25213f";
  std::wstring image;
  bool gridLines = true;
  bool perButton = false;
};

struct Profile {
  std::array<ButtonConfig, kButtonCount> buttons;
  ThemeConfig theme;
};

struct ProfileCollection {
  std::wstring currentProfile = L"My Deck";
  std::vector<std::wstring> profileNames{L"My Deck"};
  std::vector<Profile> profiles{Profile{}};
};

Profile MakeDefaultProfile();
std::wstring ActionTypeName(ActionType type);
ActionType ParseActionType(const std::wstring& value);
std::wstring BackgroundModeName(BackgroundMode mode);

}  // namespace crowdeck
