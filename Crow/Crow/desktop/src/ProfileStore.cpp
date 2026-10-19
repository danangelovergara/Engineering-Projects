#include "crowdeck/ProfileStore.h"

#include "crowdeck/Json.h"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace crowdeck {
namespace {

using json::Value;

bool BoolField(const Value& object, const char* name, bool fallback) {
  const Value* value = object.Find(name);
  if (!value) return fallback;
  const auto* boolean = std::get_if<bool>(&value->data);
  if (!boolean) throw std::runtime_error(std::string("Invalid boolean field: ") + name);
  return *boolean;
}

bool IsColor(const std::wstring& value) {
  if (value.size() != 7 || value[0] != L'#') return false;
  for (size_t index = 1; index < value.size(); ++index) {
    if (!((value[index] >= L'0' && value[index] <= L'9') ||
          (value[index] >= L'a' && value[index] <= L'f') ||
          (value[index] >= L'A' && value[index] <= L'F'))) {
      return false;
    }
  }
  return true;
}

Value MakeString(const std::wstring& value) {
  return Value(ProfileStore::WideToUtf8(value));
}

Value ButtonToJson(const ButtonConfig& button) {
  Value::Object action;
  switch (button.action.type) {
    case ActionType::LaunchApplication:
      action.emplace("type", "launch_app");
      break;
    case ActionType::Keystrokes:
      action.emplace("type", "keystrokes");
      break;
    case ActionType::OpenUrl:
      action.emplace("type", "open_url");
      break;
    case ActionType::RunScript:
      action.emplace("type", "run_script");
      break;
    default:
      action.emplace("type", "do_nothing");
      break;
  }
  action.emplace("value", MakeString(button.action.value));
  Value::Object object;
  object.emplace("action", Value(std::move(action)));
  object.emplace("color", MakeString(button.color));
  object.emplace("icon", MakeString(button.icon));
  object.emplace("icon_path", MakeString(button.iconPath));
  object.emplace("label", MakeString(button.label));
  return Value(std::move(object));
}

Value ThemeToJson(const ThemeConfig& theme) {
  Value::Object object;
  object.emplace("color1", MakeString(theme.color1));
  object.emplace("color2", MakeString(theme.color2));
  object.emplace("grid_lines", Value(theme.gridLines));
  object.emplace("image", MakeString(theme.image));
  object.emplace("mode", ProfileStore::WideToUtf8(BackgroundModeName(theme.mode)));
  object.emplace("per_button", Value(theme.perButton));
  return Value(std::move(object));
}

bool TryGetString(const Value& object, const char* name, std::wstring* result) {
  const Value* value = object.Find(name);
  if (!value) return false;
  const auto* text = value->AsString();
  if (!text) throw std::runtime_error(std::string("Invalid string field: ") + name);
  *result = ProfileStore::Utf8ToWide(*text);
  return true;
}

Profile LoadProfile(const Value& value) {
  const auto* array = value.AsArray();
  if (!array || array->size() != kButtonCount) {
    throw std::runtime_error("Each profile must contain exactly 15 buttons.");
  }
  Profile profile = MakeDefaultProfile();
  for (size_t index = 0; index < array->size(); ++index) {
    const Value& source = (*array)[index];
    if (!source.AsObject()) throw std::runtime_error("Button entries must be objects.");
    ButtonConfig& button = profile.buttons[index];
    TryGetString(source, "icon", &button.icon);
    TryGetString(source, "icon_path", &button.iconPath);
    TryGetString(source, "label", &button.label);
    TryGetString(source, "color", &button.color);
    if (!IsColor(button.color)) throw std::runtime_error("A button contains an invalid color.");

    if (const Value* action = source.Find("action")) {
      if (!action->AsObject()) throw std::runtime_error("Button action must be an object.");
      std::wstring type;
      TryGetString(*action, "type", &type);
      button.action.type = ParseActionType(type);
      TryGetString(*action, "value", &button.action.value);
    } else {
      std::wstring legacyMacro;
      if (TryGetString(source, "macro", &legacyMacro) && !legacyMacro.empty()) {
        button.action.type = ActionType::Keystrokes;
        button.action.value = std::move(legacyMacro);
      }
    }
  }
  return profile;
}

ThemeConfig LoadTheme(const Value* value) {
  ThemeConfig theme;
  if (!value) return theme;
  if (!value->AsObject()) throw std::runtime_error("Theme entries must be objects.");
  std::wstring mode;
  if (TryGetString(*value, "mode", &mode)) {
    if (mode == L"gradient") theme.mode = BackgroundMode::Gradient;
    else if (mode == L"image") theme.mode = BackgroundMode::Image;
  }
  TryGetString(*value, "color1", &theme.color1);
  TryGetString(*value, "color2", &theme.color2);
  TryGetString(*value, "image", &theme.image);
  if (!IsColor(theme.color1) || !IsColor(theme.color2)) {
    throw std::runtime_error("A theme contains an invalid color.");
  }
  theme.gridLines = BoolField(*value, "grid_lines", true);
  theme.perButton = BoolField(*value, "per_button", false);
  return theme;
}

}  // namespace

ProfileStore::ProfileStore(std::filesystem::path path) : path_(std::move(path)) {}

const std::filesystem::path& ProfileStore::Path() const { return path_; }

std::filesystem::path ProfileStore::DefaultPath() {
  wchar_t appData[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    throw std::runtime_error("Could not determine the Windows application data folder.");
  }
  return std::filesystem::path(appData) / L"Crow" / L"profiles.json";
}

std::wstring ProfileStore::Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) throw std::runtime_error("Profile data contains invalid UTF-8.");
  std::wstring output(length, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                           static_cast<int>(value.size()), output.data(), length)) {
    throw std::runtime_error("Could not decode profile text.");
  }
  return output;
}

std::string ProfileStore::WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0,
                                         nullptr, nullptr);
  if (length <= 0) throw std::runtime_error("Could not encode profile text as UTF-8.");
  std::string output(length, '\0');
  if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                           static_cast<int>(value.size()), output.data(), length,
                           nullptr, nullptr)) {
    throw std::runtime_error("Could not encode profile text.");
  }
  return output;
}

ProfileCollection ProfileStore::Load(std::wstring* warning) const {
  ProfileCollection collection;
  collection.profiles[0] = MakeDefaultProfile();
  if (warning) warning->clear();
  std::ifstream file(path_, std::ios::binary);
  if (!file && path_.filename() == L"profiles.json" &&
      path_.parent_path().filename() == L"Crow") {
    file.clear();
    file.open(path_.parent_path().parent_path() / L"CrowDeck" / L"profiles.json",
              std::ios::binary);
  }
  if (!file) return collection;
  try {
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const Value root = json::Parse(buffer.str());
    const Value* profileMap = root.Find("profiles");
    if (!profileMap || !profileMap->AsObject() || profileMap->AsObject()->empty()) {
      throw std::runtime_error("The profiles field is missing or empty.");
    }
    const auto* profileObject = profileMap->AsObject();
    collection.profileNames.clear();
    collection.profiles.clear();
    for (const auto& [name, buttons] : *profileObject) {
      collection.profileNames.push_back(Utf8ToWide(name));
      collection.profiles.push_back(LoadProfile(buttons));
    }

    const Value* themes = root.Find("themes");
    for (size_t index = 0; index < collection.profileNames.size(); ++index) {
      const std::string key = WideToUtf8(collection.profileNames[index]);
      collection.profiles[index].theme = LoadTheme(themes ? themes->Find(key) : nullptr);
    }

    std::wstring current;
    if (TryGetString(root, "current_profile", &current) &&
        std::find(collection.profileNames.begin(), collection.profileNames.end(), current) !=
            collection.profileNames.end()) {
      collection.currentProfile = std::move(current);
    } else {
      collection.currentProfile = collection.profileNames.front();
    }
  } catch (const std::exception& error) {
    collection = ProfileCollection{};
    collection.profiles[0] = MakeDefaultProfile();
    if (warning) {
      *warning = L"Saved profiles could not be loaded. A fresh profile was opened.\n\n" +
                 Utf8ToWide(error.what());
    }
  }
  return collection;
}

bool ProfileStore::Save(const ProfileCollection& collection,
                        std::wstring* error) const {
  try {
    std::filesystem::create_directories(path_.parent_path());
    Value::Object profileObject;
    Value::Object themeObject;
    for (size_t index = 0; index < collection.profileNames.size(); ++index) {
      Value::Array buttons;
      for (const auto& button : collection.profiles[index].buttons) {
        buttons.push_back(ButtonToJson(button));
      }
      const std::string name = WideToUtf8(collection.profileNames[index]);
      profileObject.emplace(name, Value(std::move(buttons)));
      themeObject.emplace(name, ThemeToJson(collection.profiles[index].theme));
    }
    Value::Object root;
    root.emplace("current_profile", MakeString(collection.currentProfile));
    root.emplace("profiles", Value(std::move(profileObject)));
    root.emplace("themes", Value(std::move(themeObject)));
    const std::string serialized = json::Stringify(Value(std::move(root)));
    const std::filesystem::path temporary = path_.wstring() + L".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      if (!file) throw std::runtime_error("Could not create the profile settings file.");
      file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
      if (!file) throw std::runtime_error("Could not write all profile settings.");
    }
    if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      const DWORD code = GetLastError();
      DeleteFileW(temporary.c_str());
      throw std::runtime_error("Could not replace the profile file (Windows error " +
                               std::to_string(code) + ").");
    }
    if (error) error->clear();
    return true;
  } catch (const std::exception& exception) {
    if (error) {
      try {
        *error = Utf8ToWide(exception.what());
      } catch (...) {
        *error = L"Could not save profile settings.";
      }
    }
    return false;
  }
}

}  // namespace crowdeck
