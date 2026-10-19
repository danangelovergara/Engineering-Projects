#include "crowdeck/ActionExecutor.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace crowdeck {
namespace {

std::wstring Quote(const std::wstring& value) {
  std::wstring quoted = L"\"";
  for (wchar_t character : value) {
    if (character == L'"') quoted += L'\\';
    quoted += character;
  }
  quoted += L'"';
  return quoted;
}

WORD VirtualKey(const std::wstring& key, bool* needsShift) {
  const std::wstring upper = [&] {
    std::wstring result = key;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(towupper(value)); });
    return result;
  }();
  static const std::pair<const wchar_t*, WORD> namedKeys[] = {
      {L"SPACE", VK_SPACE},     {L"ENTER", VK_RETURN},
      {L"RETURN", VK_RETURN},   {L"TAB", VK_TAB},
      {L"ESC", VK_ESCAPE},      {L"ESCAPE", VK_ESCAPE},
      {L"BACKSPACE", VK_BACK},  {L"DELETE", VK_DELETE},
      {L"DEL", VK_DELETE},      {L"INSERT", VK_INSERT},
      {L"HOME", VK_HOME},       {L"END", VK_END},
      {L"PAGEUP", VK_PRIOR},    {L"PAGEDOWN", VK_NEXT},
      {L"UP", VK_UP},           {L"DOWN", VK_DOWN},
      {L"LEFT", VK_LEFT},       {L"RIGHT", VK_RIGHT},
      {L"CAPSLOCK", VK_CAPITAL}};
  for (const auto& [name, value] : namedKeys) {
    if (upper == name) return value;
  }
  if (upper.size() >= 2 && upper[0] == L'F') {
    try {
      const int number = std::stoi(upper.substr(1));
      if (number >= 1 && number <= 24) return static_cast<WORD>(VK_F1 + number - 1);
    } catch (...) {
      return 0;
    }
  }
  if (key.size() != 1) return 0;
  const SHORT translated = VkKeyScanW(key[0]);
  if (translated == -1) return 0;
  const BYTE modifiers = HIBYTE(translated);
  if (modifiers & 1) needsShift[0] = true;
  return LOBYTE(translated);
}

bool SendChord(const std::wstring& expression, std::wstring* error) {
  std::vector<std::wstring> parts;
  std::wistringstream stream(expression);
  std::wstring part;
  while (std::getline(stream, part, L'+')) {
    const size_t first = part.find_first_not_of(L" \t");
    const size_t last = part.find_last_not_of(L" \t");
    if (first == std::wstring::npos) continue;
    parts.push_back(part.substr(first, last - first + 1));
  }
  if (parts.empty()) {
    *error = L"Enter a keystroke such as Ctrl+Shift+M.";
    return false;
  }

  std::vector<WORD> modifiers;
  std::vector<WORD> keys;
  for (const auto& item : parts) {
    std::wstring name = item;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(towupper(value)); });
    if (name == L"CTRL" || name == L"CONTROL") {
      modifiers.push_back(VK_CONTROL);
    } else if (name == L"SHIFT") {
      modifiers.push_back(VK_SHIFT);
    } else if (name == L"ALT") {
      modifiers.push_back(VK_MENU);
    } else if (name == L"WIN" || name == L"WINDOWS") {
      modifiers.push_back(VK_LWIN);
    } else {
      bool needsShift = false;
      const WORD key = VirtualKey(item, &needsShift);
      if (!key) {
        *error = L"Unsupported key: " + item;
        return false;
      }
      if (needsShift &&
          std::find(modifiers.begin(), modifiers.end(), VK_SHIFT) == modifiers.end()) {
        modifiers.push_back(VK_SHIFT);
      }
      keys.push_back(key);
    }
  }
  if (keys.empty()) {
    *error = L"Add a key after any modifiers, for example Ctrl+Shift+M.";
    return false;
  }

  std::vector<INPUT> events;
  const auto append = [&events](WORD key, DWORD flags) {
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = key;
    event.ki.dwFlags = flags;
    events.push_back(event);
  };
  for (WORD key : modifiers) append(key, 0);
  for (WORD key : keys) append(key, 0);
  for (auto key = keys.rbegin(); key != keys.rend(); ++key) {
    append(*key, KEYEVENTF_KEYUP);
  }
  for (auto key = modifiers.rbegin(); key != modifiers.rend(); ++key) {
    append(*key, KEYEVENTF_KEYUP);
  }
  if (SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT)) !=
      events.size()) {
    *error = L"Windows could not send the keystroke (error " +
             std::to_wstring(GetLastError()) + L").";
    return false;
  }
  return true;
}

bool RunScript(const std::wstring& path, std::wstring* error) {
  std::wstring extension = std::filesystem::path(path).extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
  std::wstring command;
  if (extension == L".bat") {
    command = L"cmd.exe /d /s /c \"\"" + path + L"\"\"";
  } else if (extension == L".py") {
    command = L"py.exe " + Quote(path);
  } else {
    *error = L"Choose a .py or .bat script file.";
    return false;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_PROCESS_GROUP, nullptr,
                      std::filesystem::path(path).parent_path().c_str(),
                      &startup, &process)) {
    *error = L"Could not start the script (Windows error " +
             std::to_wstring(GetLastError()) + L").";
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace

bool ActionExecutor::Execute(const ButtonConfig& button, std::wstring* error) {
  if (error) error->clear();
  switch (button.action.type) {
    case ActionType::LaunchApplication: {
      const auto result = reinterpret_cast<INT_PTR>(
          ShellExecuteW(nullptr, L"open", button.action.value.c_str(), nullptr,
                        nullptr, SW_SHOWNORMAL));
      if (result <= 32) {
        if (error) {
          *error = L"Could not launch the application (Windows error " +
                   std::to_wstring(result) + L").";
        }
        return false;
      }
      return true;
    }
    case ActionType::OpenUrl: {
      const auto result = reinterpret_cast<INT_PTR>(
          ShellExecuteW(nullptr, L"open", button.action.value.c_str(), nullptr,
                        nullptr, SW_SHOWNORMAL));
      if (result <= 32) {
        if (error) {
          *error = L"Could not open the URL (Windows error " +
                   std::to_wstring(result) + L").";
        }
        return false;
      }
      return true;
    }
    case ActionType::Keystrokes:
      if (!error) {
        std::wstring message;
        return SendChord(button.action.value, &message);
      }
      return SendChord(button.action.value, error);
    case ActionType::RunScript: {
      std::wstring message;
      const bool success = RunScript(button.action.value, &message);
      if (!success && error) *error = std::move(message);
      return success;
    }
    case ActionType::DoNothing:
      return true;
  }
  return true;
}

}  // namespace crowdeck
