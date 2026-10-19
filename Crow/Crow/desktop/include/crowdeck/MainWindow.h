#pragma once

#include "crowdeck/DeckSerial.h"
#include "crowdeck/Models.h"
#include "crowdeck/ProfileStore.h"

#include <Windows.h>
#include <gdiplus.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace crowdeck {

class MainWindow {
 public:
  explicit MainWindow(HINSTANCE instance);
  ~MainWindow();

  bool Create(int showCommand);

 private:
  enum ControlId {
    ProfilePicker = 100,
    LabelEdit,
    ActionPicker,
    ActionValueEdit,
    BrowseActionControl,
    ChooseIconControl,
    RemoveIconControl,
    ColorEdit,
    ChooseColorControl,
    SaveButtonControl,
    CancelButtonControl,
    SaveProfileControl,
    PushLayoutControl,
    MenuButton,
    MinimizeWindowControl,
    MaximizeWindowControl,
    CloseWindowControl,
    TrayOpenControl,
    TrayExitControl,
  };

  static constexpr UINT TrayCallbackMessage = WM_APP + 1;
  static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  bool AddTrayIcon();
  void RemoveTrayIcon();
  void ShowFromTray();
  void ShowTrayMenu();
  void CreateControls();
  void PositionControls(int width, int height);
  void Paint();
  void PaintGrid(Gdiplus::Graphics& graphics, const RECT& bounds);
  void DrawButton(const DRAWITEMSTRUCT& item);
  void HandleCommand(int id, int notification);
  void SelectButton(int index);
  void LoadDraft();
  bool SaveDraft(bool showErrors = true);
  bool ConfirmDraft();
  void CancelDraft();
  void UpdateActionFields();
  void RefreshProfiles();
  void ChangeProfile(int index);
  void AddProfile();
  void RenameProfile();
  void DeleteProfile();
  void EditBackground();
  bool ChooseImage(std::wstring* path, const wchar_t* title,
                   const wchar_t* filter);
  bool PickColor(std::wstring* color);
  void ChooseIcon();
  void BrowseAction();
  void SaveProfile();
  void PushLayout();
  void QueueLayout();
  std::vector<std::vector<uint8_t>> BuildLayoutPackets();
  std::vector<uint8_t> BuildButtonPacket(int index,
                                         const ButtonConfig& button);
  std::vector<uint8_t> EncodeBackgroundJpeg(
      const std::filesystem::path& path);
  void PollDeck();
  void SetStatus(const std::wstring& text, bool online);
  void ShowProfileMenu();
  void ShowSettings();
  bool GetStartupEnabled(bool* enabled, std::wstring* error) const;
  bool SetStartupEnabled(bool enabled, std::wstring* error) const;
  void MarkProfileDirty();
  void UpdateStateIndicators();

  HINSTANCE instance_ = nullptr;
  HWND window_ = nullptr;
  HWND profilePicker_ = nullptr;
  HWND labelEdit_ = nullptr;
  HWND actionPicker_ = nullptr;
  HWND actionValueEdit_ = nullptr;
  HWND browseAction_ = nullptr;
  HWND chooseIcon_ = nullptr;
  HWND removeIcon_ = nullptr;
  HWND colorEdit_ = nullptr;
  HWND chooseColor_ = nullptr;
  HWND saveButton_ = nullptr;
  HWND cancelButton_ = nullptr;
  HWND saveProfile_ = nullptr;
  HWND pushLayout_ = nullptr;
  HWND menuButton_ = nullptr;
  HWND minimizeWindow_ = nullptr;
  HWND maximizeWindow_ = nullptr;
  HWND closeWindow_ = nullptr;
  HICON trayIcon_ = nullptr;
  HFONT font_ = nullptr;
  HBRUSH controlBrush_ = nullptr;
  ProfileStore store_;
  ProfileCollection collection_;
  ButtonConfig draft_;
  int selectedButton_ = 0;
  bool draftDirty_ = false;
  bool profileDirty_ = false;
  bool layoutSyncing_ = false;
  bool deckSynced_ = false;
  bool populatingControls_ = false;
  bool warningShown_ = false;
  bool deckOnline_ = false;
  bool trayIconAdded_ = false;
  bool exiting_ = false;
  std::wstring statusText_ = L"Searching for CrowPanel…";
  std::filesystem::path workspaceRoot_;
  std::filesystem::path backgroundDirectory_;
  std::unique_ptr<Gdiplus::Image> logoImage_;
  std::vector<std::unique_ptr<Gdiplus::Image>> gridImages_;
  DeckSerial deck_;
  ULONG_PTR gdiplusToken_ = 0;
};

}  // namespace crowdeck
