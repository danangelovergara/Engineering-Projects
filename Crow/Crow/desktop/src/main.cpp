#include "crowdeck/MainWindow.h"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine,
                    int showCommand) {
  SetProcessDPIAware();
  if (commandLine && wcsstr(commandLine, L"--background")) {
    showCommand = SW_HIDE;
  }
  try {
    crowdeck::MainWindow window(instance);
    if (!window.Create(showCommand)) {
      MessageBoxW(nullptr, L"Crow could not create its desktop window.",
                  L"Crow", MB_OK | MB_ICONERROR);
      return 1;
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
  } catch (const std::exception& error) {
    const std::wstring message = crowdeck::ProfileStore::Utf8ToWide(error.what());
    MessageBoxW(nullptr, message.c_str(), L"Crow startup error",
                MB_OK | MB_ICONERROR);
    return 1;
  }
}
