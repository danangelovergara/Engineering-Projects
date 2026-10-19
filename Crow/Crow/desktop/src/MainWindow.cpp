#include "crowdeck/MainWindow.h"
#include "crowdeck/ActionExecutor.h"

#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace crowdeck {
namespace {

constexpr wchar_t kWindowClass[] = L"CrowDeckNativeWindow";
constexpr UINT_PTR kDeckPollTimer = 1;
constexpr UINT kDeckPollIntervalMs = 100;
constexpr COLORREF kBackground = RGB(8, 9, 11);
constexpr COLORREF kSurface = RGB(17, 19, 23);
constexpr COLORREF kTile = RGB(26, 29, 34);
constexpr COLORREF kText = RGB(242, 245, 248);
constexpr COLORREF kMuted = RGB(151, 162, 175);
constexpr COLORREF kAccent = RGB(139, 124, 255);
constexpr int kCrowIconResource = 101;
constexpr wchar_t kStartupRegistryPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kStartupRegistryValue[] = L"Crow";
constexpr UINT kStartupMenuCommand = 6;

UINT TaskbarCreatedMessage() {
  static const UINT message = RegisterWindowMessageW(L"TaskbarCreated");
  return message;
}

struct PromptState {
  HWND edit = nullptr;
  bool accepted = false;
  std::wstring result;
};

LRESULT CALLBACK PromptWindowProc(HWND window, UINT message, WPARAM wParam,
                                  LPARAM lParam) {
  auto* state = reinterpret_cast<PromptState*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    state = static_cast<PromptState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (message == WM_CREATE) {
    CreateWindowExW(0, L"STATIC", L"Profile name:", WS_CHILD | WS_VISIBLE,
                    16, 15, 320, 22, window, nullptr,
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                    nullptr);
    state->edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", state->result.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, 40, 320, 26,
        window, reinterpret_cast<HMENU>(10),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
        nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 174, 78,
                    76, 28, window, reinterpret_cast<HMENU>(IDCANCEL),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                    nullptr);
    CreateWindowExW(0, L"BUTTON", L"OK",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 258,
                    78, 78, 28, window, reinterpret_cast<HMENU>(IDOK),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                    nullptr);
    SendMessageW(state->edit, EM_SETLIMITTEXT, 60, 0);
    return 0;
  }
  if (message == WM_COMMAND && state) {
    if (LOWORD(wParam) == IDOK) {
      const int length = GetWindowTextLengthW(state->edit);
      state->result.assign(static_cast<size_t>(length) + 1, L'\0');
      GetWindowTextW(state->edit, state->result.data(), length + 1);
      state->result.resize(length);
      state->accepted = !state->result.empty();
      DestroyWindow(window);
      return 0;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      DestroyWindow(window);
      return 0;
    }
  }
  if (message == WM_CLOSE) {
    DestroyWindow(window);
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

bool PromptText(HWND owner, HINSTANCE instance, const wchar_t* title,
                std::wstring* value) {
  static const wchar_t promptClass[] = L"CrowDeckTextPrompt";
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = PromptWindowProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    type.lpszClassName = promptClass;
    registered = RegisterClassExW(&type) != 0 ||
                 GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }
  if (!registered) return false;
  PromptState state;
  state.result = *value;
  RECT parent{};
  GetWindowRect(owner, &parent);
  const int x = parent.left + ((parent.right - parent.left) - 360) / 2;
  const int y = parent.top + ((parent.bottom - parent.top) - 140) / 2;
  HWND dialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME, promptClass, title,
      WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 360, 140, owner, nullptr,
      instance, &state);
  if (!dialog) return false;
  EnableWindow(owner, FALSE);
  ShowWindow(dialog, SW_SHOW);
  SetFocus(state.edit);
  SendMessageW(state.edit, EM_SETSEL, 0, -1);
  MSG message{};
  while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dialog, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  if (state.accepted) *value = std::move(state.result);
  return state.accepted;
}

std::string UrlEncode(const std::wstring& value) {
  const std::string utf8 = ProfileStore::WideToUtf8(value);
  constexpr char hex[] = "0123456789ABCDEF";
  std::string output;
  for (unsigned char character : utf8) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '_' || character == '.' || character == '~') {
      output.push_back(static_cast<char>(character));
    } else {
      output.push_back('%');
      output.push_back(hex[character >> 4]);
      output.push_back(hex[character & 0x0F]);
    }
  }
  return output;
}

std::vector<uint8_t> AsciiPacket(const std::string& line) {
  return std::vector<uint8_t>(line.begin(), line.end());
}

std::wstring GetWindowTextValue(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring value(length + 1, L'\0');
  if (length > 0) GetWindowTextW(control, value.data(), length + 1);
  value.resize(length);
  return value;
}

COLORREF ParseColor(const std::wstring& color) {
  if (color.size() != 7 || color[0] != L'#') return kAccent;
  const auto component = [&color](size_t offset) {
    return static_cast<BYTE>(std::wcstoul(color.substr(offset, 2).c_str(), nullptr, 16));
  };
  return RGB(component(1), component(3), component(5));
}

bool IsColorValue(const std::wstring& color) {
  if (color.size() != 7 || color[0] != L'#') return false;
  return std::all_of(color.begin() + 1, color.end(), [](wchar_t character) {
    return (character >= L'0' && character <= L'9') ||
           (character >= L'a' && character <= L'f') ||
           (character >= L'A' && character <= L'F');
  });
}

std::wstring ColorString(COLORREF color) {
  wchar_t value[8]{};
  swprintf_s(value, L"#%02X%02X%02X", GetRValue(color), GetGValue(color),
             GetBValue(color));
  return value;
}

int GetEncoderClsid(const WCHAR* mimeType, CLSID* clsid) {
  UINT count = 0;
  UINT bytes = 0;
  if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok ||
      bytes == 0) {
    return -1;
  }
  std::vector<BYTE> buffer(bytes);
  auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
  if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return -1;
  for (UINT index = 0; index < count; ++index) {
    if (wcscmp(encoders[index].MimeType, mimeType) == 0) {
      *clsid = encoders[index].Clsid;
      return static_cast<int>(index);
    }
  }
  return -1;
}

void DrawText(Gdiplus::Graphics& graphics, const std::wstring& text,
              Gdiplus::Font& font, Gdiplus::Brush& brush,
              const Gdiplus::RectF& bounds, Gdiplus::StringAlignment alignment) {
  Gdiplus::StringFormat format;
  format.SetAlignment(alignment);
  format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
  graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, bounds,
                      &format, &brush);
}

struct CropDialogState {
  std::unique_ptr<Gdiplus::Image> image;
  std::filesystem::path destination;
  float centerX = 0.0f;
  float centerY = 0.0f;
  float zoom = 1.0f;
  int lastX = 0;
  int lastY = 0;
  bool dragging = false;
  bool accepted = false;
  bool removeEdgeBackground = false;
};

constexpr int kCropPreviewLeft = 80;
constexpr int kCropPreviewTop = 56;
constexpr int kCropPreviewSize = 320;
constexpr int kCropRemoveBackgroundControl = 3;

Gdiplus::RectF CropSource(const CropDialogState& state) {
  const float width = static_cast<float>(state.image->GetWidth());
  const float height = static_cast<float>(state.image->GetHeight());
  const float side = std::min(width, height) / state.zoom;
  const float left = std::clamp(state.centerX - side / 2.0f, 0.0f,
                                width - side);
  const float top = std::clamp(state.centerY - side / 2.0f, 0.0f,
                               height - side);
  return Gdiplus::RectF(left, top, side, side);
}

bool RemoveEdgeBackground(Gdiplus::Bitmap& bitmap) {
  const int width = static_cast<int>(bitmap.GetWidth());
  const int height = static_cast<int>(bitmap.GetHeight());
  Gdiplus::BitmapData data{};
  Gdiplus::Rect bounds(0, 0, width, height);
  if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead |
                                  Gdiplus::ImageLockModeWrite,
                      PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
    return false;
  }

  auto* pixels = static_cast<uint8_t*>(data.Scan0);
  const auto colorAt = [&pixels, &data](int x, int y) {
    const auto* row = reinterpret_cast<const uint32_t*>(
        pixels + static_cast<ptrdiff_t>(y) * data.Stride);
    return row[x];
  };
  const auto isOpaque = [&colorAt](int x, int y) {
    return (colorAt(x, y) >> 24) != 0;
  };
  const auto nearColor = [](uint32_t pixel, uint32_t background) {
    constexpr int tolerance = 28;
    const auto channel = [](uint32_t color, int shift) {
      return static_cast<int>((color >> shift) & 0xff);
    };
    return (pixel >> 24) != 0 &&
           std::abs(channel(pixel, 16) - channel(background, 16)) <= tolerance &&
           std::abs(channel(pixel, 8) - channel(background, 8)) <= tolerance &&
           std::abs(channel(pixel, 0) - channel(background, 0)) <= tolerance;
  };
  auto setTransparent = [&pixels, &data](int x, int y) {
    auto* row = reinterpret_cast<uint32_t*>(
        pixels + static_cast<ptrdiff_t>(y) * data.Stride);
    row[x] &= 0x00ffffff;
  };

  std::vector<uint8_t> removed(static_cast<size_t>(width * height), 0);
  std::vector<size_t> pending;
  const auto removeFromSeed = [&](int seedX, int seedY) {
    if (!isOpaque(seedX, seedY)) return;
    const uint32_t background = colorAt(seedX, seedY);
    std::vector<uint8_t> visited(static_cast<size_t>(width * height), 0);
    pending.clear();
    pending.push_back(static_cast<size_t>(seedY * width + seedX));
    visited[pending.back()] = 1;
    while (!pending.empty()) {
      const size_t index = pending.back();
      pending.pop_back();
      const int x = static_cast<int>(index % width);
      const int y = static_cast<int>(index / width);
      const uint32_t pixel = colorAt(x, y);
      if (!nearColor(pixel, background)) continue;
      removed[index] = 1;
      for (const auto& [nextX, nextY] :
           std::array<std::pair<int, int>, 4>{{{x - 1, y}, {x + 1, y},
                                              {x, y - 1}, {x, y + 1}}}) {
        if (nextX < 0 || nextX >= width || nextY < 0 || nextY >= height) {
          continue;
        }
        const size_t next = static_cast<size_t>(nextY * width + nextX);
        if (!visited[next]) {
          visited[next] = 1;
          pending.push_back(next);
        }
      }
    }
  };

  for (int position = 0; position < width; position += 4) {
    removeFromSeed(position, 0);
    removeFromSeed(position, height - 1);
  }
  for (int position = 0; position < height; position += 4) {
    removeFromSeed(0, position);
    removeFromSeed(width - 1, position);
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (removed[static_cast<size_t>(y * width + x)]) setTransparent(x, y);
    }
  }
  return bitmap.UnlockBits(&data) == Gdiplus::Ok;
}

std::unique_ptr<Gdiplus::Bitmap> RenderCroppedImage(
    const CropDialogState& state, std::wstring* error) {
  auto output =
      std::make_unique<Gdiplus::Bitmap>(100, 100, PixelFormat32bppARGB);
  Gdiplus::Graphics graphics(output.get());
  if (graphics.GetLastStatus() != Gdiplus::Ok) {
    *error = L"Crow could not prepare the cropped PNG image.";
    return nullptr;
  }
  graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  Gdiplus::GraphicsPath mask;
  mask.AddArc(0, 0, 20, 20, 180, 90);
  mask.AddArc(80, 0, 20, 20, 270, 90);
  mask.AddArc(80, 80, 20, 20, 0, 90);
  mask.AddArc(0, 80, 20, 20, 90, 90);
  mask.CloseFigure();
  graphics.SetClip(&mask);
  const Gdiplus::RectF source = CropSource(state);
  const Gdiplus::Status drawStatus = graphics.DrawImage(
      state.image.get(), Gdiplus::RectF(0, 0, 100, 100), source.X, source.Y,
      source.Width, source.Height, Gdiplus::UnitPixel);
  if (drawStatus != Gdiplus::Ok ||
      (state.removeEdgeBackground && !RemoveEdgeBackground(*output))) {
    *error = L"Crow could not prepare the cropped PNG image.";
    return nullptr;
  }
  return output;
}

bool SaveCroppedPng(const CropDialogState& state, std::wstring* error) {
  auto output = RenderCroppedImage(state, error);
  if (!output) return false;
  CLSID encoder{};
  if (GetEncoderClsid(L"image/png", &encoder) < 0 ||
      output->Save(state.destination.c_str(), &encoder, nullptr) !=
          Gdiplus::Ok) {
    std::error_code fileError;
    std::filesystem::remove(state.destination, fileError);
    *error = L"Crow could not prepare the cropped PNG image.";
    return false;
  }
  std::error_code fileError;
  const uintmax_t fileSize = std::filesystem::file_size(state.destination, fileError);
  if (fileError || fileSize == 0 || fileSize > 65536) {
    std::filesystem::remove(state.destination, fileError);
    *error = L"The cropped image could not be saved under the deck's 64 KB image limit.";
    return false;
  }
  return true;
}

LRESULT CALLBACK CropDialogProc(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  auto* state = reinterpret_cast<CropDialogState*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    state = static_cast<CropDialogState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }
  if (!state) return DefWindowProcW(window, message, wParam, lParam);
  switch (message) {
    case WM_CREATE:
      CreateWindowExW(0, L"BUTTON", L"Remove edge background",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                      48, 412, 166, 28, window,
                      reinterpret_cast<HMENU>(kCropRemoveBackgroundControl),
                      reinterpret_cast<HINSTANCE>(
                          GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                      nullptr);
      CreateWindowExW(0, L"BUTTON", L"Cancel",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                      250, 410, 82, 30, window,
                      reinterpret_cast<HMENU>(IDCANCEL),
                      reinterpret_cast<HINSTANCE>(
                          GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                      nullptr);
      CreateWindowExW(0, L"BUTTON", L"Use image",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                      340, 410, 100, 30, window,
                      reinterpret_cast<HMENU>(IDOK),
                      reinterpret_cast<HINSTANCE>(
                          GetWindowLongPtrW(window, GWLP_HINSTANCE)),
                      nullptr);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(window, &paint);
      Gdiplus::Graphics graphics(dc);
      graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      graphics.Clear(Gdiplus::Color(255, 8, 9, 11));
      Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 242, 245, 248));
      Gdiplus::SolidBrush mutedBrush(Gdiplus::Color(255, 151, 162, 175));
      Gdiplus::SolidBrush previewBackground(Gdiplus::Color(255, 23, 27, 34));
      Gdiplus::Font font(L"Segoe UI", 10.0f);
      Gdiplus::Font smallFont(L"Segoe UI", 8.5f);
      DrawText(graphics, L"Drag to position  ·  Scroll to zoom", font,
               textBrush,
               Gdiplus::RectF(40, 15, 400, 28),
               Gdiplus::StringAlignmentCenter);
      const Gdiplus::RectF preview(
          static_cast<float>(kCropPreviewLeft),
          static_cast<float>(kCropPreviewTop),
          static_cast<float>(kCropPreviewSize),
          static_cast<float>(kCropPreviewSize));
      graphics.FillRectangle(&previewBackground, preview);
      std::wstring renderError;
      auto croppedImage = RenderCroppedImage(*state, &renderError);
      if (croppedImage) {
        graphics.DrawImage(croppedImage.get(), preview);
      } else {
        DrawText(graphics, L"Could not render this image.", smallFont,
                 mutedBrush, preview, Gdiplus::StringAlignmentCenter);
      }
      Gdiplus::GraphicsPath mask;
      mask.AddArc(preview.X, preview.Y, 20.0f, 20.0f, 180.0f, 90.0f);
      mask.AddArc(preview.GetRight() - 20.0f, preview.Y, 20.0f, 20.0f,
                  270.0f, 90.0f);
      mask.AddArc(preview.GetRight() - 20.0f, preview.GetBottom() - 20.0f,
                  20.0f, 20.0f, 0.0f, 90.0f);
      mask.AddArc(preview.X, preview.GetBottom() - 20.0f, 20.0f, 20.0f,
                  90.0f, 90.0f);
      mask.CloseFigure();
      Gdiplus::Pen outline(Gdiplus::Color(255, 139, 124, 255), 2.0f);
      graphics.DrawPath(&outline, &mask);
      DrawText(graphics,
               L"Optionally remove a solid background connected to the image edge.",
               smallFont, mutedBrush, Gdiplus::RectF(40, 380, 400, 22),
               Gdiplus::StringAlignmentCenter);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const int x = GET_X_LPARAM(lParam);
      const int y = GET_Y_LPARAM(lParam);
      if (x >= kCropPreviewLeft && x < kCropPreviewLeft + kCropPreviewSize &&
          y >= kCropPreviewTop && y < kCropPreviewTop + kCropPreviewSize) {
        state->dragging = true;
        state->lastX = x;
        state->lastY = y;
        SetCapture(window);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (state->dragging) {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const Gdiplus::RectF source = CropSource(*state);
        const float factor = source.Width / kCropPreviewSize;
        state->centerX -= (x - state->lastX) * factor;
        state->centerY -= (y - state->lastY) * factor;
        state->lastX = x;
        state->lastY = y;
        InvalidateRect(window, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      if (state->dragging) {
        state->dragging = false;
        ReleaseCapture();
      }
      return 0;
    case WM_MOUSEWHEEL: {
      const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
      state->zoom = std::clamp(
          state->zoom * std::pow(1.2f, static_cast<float>(notches)), 1.0f, 4.0f);
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == kCropRemoveBackgroundControl) {
        state->removeEdgeBackground =
            SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK, 0, 0) ==
            BST_CHECKED;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      }
      if (LOWORD(wParam) == IDOK) {
        std::wstring error;
        if (!SaveCroppedPng(*state, &error)) {
          MessageBoxW(window, error.c_str(), L"Could not use image",
                      MB_OK | MB_ICONERROR);
          return 0;
        }
        state->accepted = true;
        DestroyWindow(window);
        return 0;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        DestroyWindow(window);
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}

bool CropImage(HWND owner, HINSTANCE instance,
               const std::filesystem::path& sourcePath,
               const std::filesystem::path& destination,
               std::wstring* error) {
  static constexpr wchar_t cropClass[] = L"CrowDeckImageCropDialog";
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = CropDialogProc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.lpszClassName = cropClass;
    registered = RegisterClassExW(&type) != 0 ||
                 GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }
  if (!registered) {
    *error = L"Could not open the image crop window.";
    return false;
  }
  CropDialogState state;
  state.image = std::make_unique<Gdiplus::Image>(sourcePath.c_str());
  if (state.image->GetLastStatus() != Gdiplus::Ok ||
      state.image->GetWidth() == 0 || state.image->GetHeight() == 0) {
    *error = L"Crow could not read the selected image.";
    return false;
  }
  state.centerX = state.image->GetWidth() / 2.0f;
  state.centerY = state.image->GetHeight() / 2.0f;
  state.destination = destination;
  RECT parent{};
  GetWindowRect(owner, &parent);
  const int x = parent.left + ((parent.right - parent.left) - 500) / 2;
  const int y = parent.top + ((parent.bottom - parent.top) - 500) / 2;
  HWND dialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME, cropClass, L"Crop button artwork",
      WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, 500, 500, owner, nullptr,
      instance, &state);
  if (!dialog) {
    *error = L"Could not create the image crop window.";
    return false;
  }
  EnableWindow(owner, FALSE);
  ShowWindow(dialog, SW_SHOW);
  UpdateWindow(dialog);
  MSG message{};
  while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dialog, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  EnableWindow(owner, TRUE);
  SetForegroundWindow(owner);
  return state.accepted;
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance)
    : instance_(instance), store_(ProfileStore::DefaultPath()) {
  Gdiplus::GdiplusStartupInput startupInput;
  Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr);
  const auto executableDirectory =
      std::filesystem::path([&] {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                static_cast<DWORD>(path.size()));
        path.resize(length);
        return path;
      }())
          .parent_path();
  workspaceRoot_ = executableDirectory.parent_path().parent_path();
  if (!std::filesystem::exists(workspaceRoot_ / L"platformio.ini")) {
    workspaceRoot_ = std::filesystem::current_path();
  }
  backgroundDirectory_ = workspaceRoot_ / L"assets" / L"backgrounds";
  std::filesystem::create_directories(backgroundDirectory_);
  const auto logoPath = workspaceRoot_ / L"assets" / L"crow-logo.jpg";
  if (std::filesystem::exists(logoPath)) {
    auto image = std::make_unique<Gdiplus::Image>(logoPath.c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) logoImage_ = std::move(image);
  }
  std::wstring warning;
  collection_ = store_.Load(&warning);
  if (!warning.empty()) {
    statusText_ = warning;
    warningShown_ = true;
  }
  font_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                      L"Segoe UI");
  controlBrush_ = CreateSolidBrush(kSurface);
}

MainWindow::~MainWindow() {
  deck_.Stop();
  RemoveTrayIcon();
  if (trayIcon_) DestroyIcon(trayIcon_);
  if (font_) DeleteObject(font_);
  if (controlBrush_) DeleteObject(controlBrush_);
  if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

bool MainWindow::Create(int showCommand) {
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES |
                                                    ICC_USEREX_CLASSES};
  InitCommonControlsEx(&controls);

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_DBLCLKS;
  windowClass.lpfnWndProc = WindowProc;
  windowClass.hInstance = instance_;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  trayIcon_ = static_cast<HICON>(LoadImageW(
      instance_, MAKEINTRESOURCEW(kCrowIconResource), IMAGE_ICON, 32, 32,
      LR_DEFAULTCOLOR));
  windowClass.hIcon = trayIcon_ ? trayIcon_ : LoadIconW(nullptr, IDI_APPLICATION);
  windowClass.hIconSm = windowClass.hIcon;
  windowClass.hbrBackground = CreateSolidBrush(kBackground);
  windowClass.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&windowClass) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }
  window_ = CreateWindowExW(0, kWindowClass, L"Crow",
                            WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                                WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1160, 820, nullptr,
                            nullptr, instance_, this);
  if (!window_) return false;
  const COLORREF noDwmBorder = DWMWA_COLOR_NONE;
  DwmSetWindowAttribute(window_, DWMWA_BORDER_COLOR, &noDwmBorder,
                        sizeof(noDwmBorder));
  if (!AddTrayIcon()) {
    MessageBoxW(window_,
                L"Crow could not add its system-tray icon. Closing the window "
                L"will exit the app until the tray icon is available.",
                L"Crow tray icon", MB_OK | MB_ICONWARNING);
  }
  ShowWindow(window_, showCommand);
  UpdateWindow(window_);
  deck_.Start();
  SetTimer(window_, kDeckPollTimer, kDeckPollIntervalMs, nullptr);
  if (warningShown_) {
    MessageBoxW(window_, statusText_.c_str(), L"Crow settings",
                MB_OK | MB_ICONWARNING);
    SetStatus(L"Deck disconnected · retrying", false);
  }
  return true;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM wParam,
                                        LPARAM lParam) {
  MainWindow* self = reinterpret_cast<MainWindow*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<MainWindow*>(create->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(window, message, wParam, lParam);
  return self->HandleMessage(message, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == TaskbarCreatedMessage()) {
    trayIconAdded_ = false;
    if (!AddTrayIcon()) {
      MessageBoxW(window_,
                  L"Crow could not restore its system-tray icon after "
                  L"Explorer restarted. Closing the window will exit the app.",
                  L"Crow tray icon", MB_OK | MB_ICONWARNING);
    }
    return 0;
  }
  switch (message) {
    case WM_CREATE:
      CreateControls();
      return 0;
    case WM_NCCALCSIZE:
      if (wParam) return 0;
      break;
    case WM_NCACTIVATE:
      return TRUE;
    case WM_NCPAINT:
      return 0;
    case WM_NCHITTEST: {
      const LRESULT hit = DefWindowProcW(window_, message, wParam, lParam);
      if (hit != HTCLIENT) return hit;
      if (IsZoomed(window_)) return HTCLIENT;
      RECT bounds{};
      GetWindowRect(window_, &bounds);
      const int x = GET_X_LPARAM(lParam);
      const int y = GET_Y_LPARAM(lParam);
      constexpr int edge = 8;
      const bool left = x < bounds.left + edge;
      const bool right = x >= bounds.right - edge;
      const bool top = y < bounds.top + edge;
      const bool bottom = y >= bounds.bottom - edge;
      if (top && left) return HTTOPLEFT;
      if (top && right) return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT;
      if (bottom && right) return HTBOTTOMRIGHT;
      if (left) return HTLEFT;
      if (right) return HTRIGHT;
      if (top) return HTTOP;
      if (bottom) return HTBOTTOM;
      return HTCLIENT;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_GETMINMAXINFO: {
      auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
      limits->ptMinTrackSize.x = 980;
      limits->ptMinTrackSize.y = 700;
      return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, RGB(238, 241, 246));
      SetBkColor(dc, kSurface);
      return reinterpret_cast<LRESULT>(controlBrush_);
    }
    case WM_CTLCOLORBTN: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, RGB(238, 241, 246));
      SetBkColor(dc, kSurface);
      return reinterpret_cast<LRESULT>(controlBrush_);
    }
    case WM_SIZE:
      if (wParam == SIZE_MINIMIZED) return 0;
      PositionControls(LOWORD(lParam), HIWORD(lParam));
      InvalidateRect(window_, nullptr, TRUE);
      return 0;
    case WM_PAINT:
      Paint();
      return 0;
    case WM_DRAWITEM:
      if (lParam) {
        DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
      }
      return TRUE;
    case WM_COMMAND:
      if (LOWORD(wParam) == TrayOpenControl) {
        ShowFromTray();
        return 0;
      }
      if (LOWORD(wParam) == TrayExitControl) {
        exiting_ = true;
        RemoveTrayIcon();
        DestroyWindow(window_);
        return 0;
      }
      HandleCommand(LOWORD(wParam), HIWORD(wParam));
      return 0;
    case TrayCallbackMessage:
      if (lParam == WM_LBUTTONDBLCLK) {
        ShowFromTray();
        return 0;
      }
      if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
        ShowTrayMenu();
        return 0;
      }
      return 0;
    case WM_TIMER:
      if (wParam == kDeckPollTimer) PollDeck();
      return 0;
    case WM_LBUTTONDBLCLK:
      if (GET_Y_LPARAM(lParam) < 68) {
        ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
        return 0;
      }
      break;
    case WM_LBUTTONDOWN: {
      const int x = GET_X_LPARAM(lParam);
      const int y = GET_Y_LPARAM(lParam);
      if (y < 68) {
        ReleaseCapture();
        SendMessageW(window_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
      }
      RECT client{};
      GetClientRect(window_, &client);
      const int panelHeight = std::max(180, static_cast<int>(client.bottom - 274));
      const int gridLeft = 44;
      const int gridTop = 192;
      const int gridWidth =
          std::max(100, static_cast<int>((client.right - 56) * 0.63) - 32);
      const int gridHeight = std::max(80, panelHeight - 66);
      const float scale =
          std::min(gridWidth / 800.0f, gridHeight / 480.0f);
      const float boardLeft = gridLeft + (gridWidth - 800.0f * scale) / 2.0f;
      const float boardTop = gridTop + (gridHeight - 480.0f * scale) / 2.0f;
      if (scale > 0 && x >= boardLeft && x < boardLeft + 800.0f * scale &&
          y >= boardTop && y < boardTop + 480.0f * scale) {
        const int column = std::clamp(
            static_cast<int>((x - boardLeft) / (160.0f * scale)), 0, 4);
        const int row = std::clamp(
            static_cast<int>((y - boardTop) / (160.0f * scale)), 0, 2);
        SelectButton(row * 5 + column);
      }
      return 0;
    }
    case WM_CLOSE:
      if (trayIconAdded_ && !exiting_) {
        ShowWindow(window_, SW_HIDE);
      } else {
        DestroyWindow(window_);
      }
      return 0;
    case WM_DESTROY:
      KillTimer(window_, kDeckPollTimer);
      RemoveTrayIcon();
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window_, message, wParam, lParam);
  }
  return DefWindowProcW(window_, message, wParam, lParam);
}

bool MainWindow::AddTrayIcon() {
  if (trayIconAdded_) return true;
  NOTIFYICONDATAW icon{};
  icon.cbSize = sizeof(icon);
  icon.hWnd = window_;
  icon.uID = 1;
  icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  icon.uCallbackMessage = TrayCallbackMessage;
  icon.hIcon = trayIcon_ ? trayIcon_ : LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(icon.szTip, L"Crow - Control Deck");
  if (!Shell_NotifyIconW(NIM_ADD, &icon)) return false;
  trayIconAdded_ = true;
  return true;
}

void MainWindow::RemoveTrayIcon() {
  if (!trayIconAdded_) return;
  NOTIFYICONDATAW icon{};
  icon.cbSize = sizeof(icon);
  icon.hWnd = window_;
  icon.uID = 1;
  Shell_NotifyIconW(NIM_DELETE, &icon);
  trayIconAdded_ = false;
}

void MainWindow::ShowFromTray() {
  if (IsIconic(window_)) ShowWindow(window_, SW_RESTORE);
  else ShowWindow(window_, SW_SHOW);
  SetForegroundWindow(window_);
}

void MainWindow::ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  AppendMenuW(menu, MF_STRING, TrayOpenControl, L"Open Crow");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, TrayExitControl, L"Exit Crow");
  POINT cursor{};
  GetCursorPos(&cursor);
  SetForegroundWindow(window_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                 cursor.x, cursor.y, 0, window_, nullptr);
  PostMessageW(window_, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

void MainWindow::CreateControls() {
  const DWORD textStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
  const DWORD comboStyle = textStyle | CBS_DROPDOWNLIST | WS_VSCROLL;
  profilePicker_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, comboStyle,
                                   0, 0, 200, 240, window_,
                                   reinterpret_cast<HMENU>(ProfilePicker),
                                   instance_, nullptr);
  labelEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               textStyle | ES_AUTOHSCROLL, 0, 0, 100, 28,
                               window_, reinterpret_cast<HMENU>(LabelEdit),
                               instance_, nullptr);
  actionPicker_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, comboStyle,
                                  0, 0, 200, 240, window_,
                                  reinterpret_cast<HMENU>(ActionPicker),
                                  instance_, nullptr);
  actionValueEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     textStyle | ES_AUTOHSCROLL, 0, 0, 100, 28,
                                     window_, reinterpret_cast<HMENU>(ActionValueEdit),
                                     instance_, nullptr);
  browseAction_ = CreateWindowExW(0, L"BUTTON", L"Browse…",
                                  textStyle | BS_OWNERDRAW, 0, 0, 80, 28,
                                  window_, reinterpret_cast<HMENU>(BrowseActionControl),
                                  instance_, nullptr);
  chooseIcon_ = CreateWindowExW(0, L"BUTTON", L"Choose image…",
                                textStyle | BS_OWNERDRAW, 0, 0, 90, 28,
                                window_, reinterpret_cast<HMENU>(ChooseIconControl),
                                instance_, nullptr);
  removeIcon_ = CreateWindowExW(0, L"BUTTON", L"Clear image",
                                 textStyle | BS_OWNERDRAW, 0, 0, 90, 28,
                                 window_, reinterpret_cast<HMENU>(RemoveIconControl),
                                 instance_, nullptr);
  colorEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              textStyle | ES_UPPERCASE | ES_AUTOHSCROLL,
                              0, 0, 100, 28, window_,
                              reinterpret_cast<HMENU>(ColorEdit), instance_,
                              nullptr);
  chooseColor_ = CreateWindowExW(0, L"BUTTON", L"Choose color…",
                                 textStyle | BS_OWNERDRAW, 0, 0, 90, 28,
                                 window_, reinterpret_cast<HMENU>(ChooseColorControl),
                                 instance_, nullptr);
  saveButton_ = CreateWindowExW(0, L"BUTTON", L"Save button",
                                textStyle | BS_OWNERDRAW, 0, 0, 100, 32,
                                window_, reinterpret_cast<HMENU>(SaveButtonControl),
                                instance_, nullptr);
  cancelButton_ = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                  textStyle | BS_OWNERDRAW, 0, 0, 90, 32,
                                  window_, reinterpret_cast<HMENU>(CancelButtonControl),
                                  instance_, nullptr);
  saveProfile_ = CreateWindowExW(0, L"BUTTON", L"Save profile",
                                 textStyle | BS_OWNERDRAW, 0, 0, 110, 36,
                                 window_, reinterpret_cast<HMENU>(SaveProfileControl),
                                 instance_, nullptr);
  pushLayout_ = CreateWindowExW(0, L"BUTTON", L"Push to deck  →",
                                textStyle | BS_OWNERDRAW, 0, 0, 150, 36,
                                window_, reinterpret_cast<HMENU>(PushLayoutControl),
                                instance_, nullptr);
  menuButton_ = CreateWindowExW(0, L"BUTTON", L"⋯",
                                textStyle | BS_OWNERDRAW, 0, 0, 36, 36,
                                window_, reinterpret_cast<HMENU>(MenuButton),
                                instance_, nullptr);
  minimizeWindow_ = CreateWindowExW(0, L"BUTTON", L"—",
                                     textStyle | BS_OWNERDRAW, 0, 0, 38, 34,
                                     window_, reinterpret_cast<HMENU>(MinimizeWindowControl),
                                     instance_, nullptr);
  maximizeWindow_ = CreateWindowExW(0, L"BUTTON", L"□",
                                     textStyle | BS_OWNERDRAW, 0, 0, 38, 34,
                                     window_, reinterpret_cast<HMENU>(MaximizeWindowControl),
                                     instance_, nullptr);
  closeWindow_ = CreateWindowExW(0, L"BUTTON", L"×",
                                  textStyle | BS_OWNERDRAW, 0, 0, 38, 34,
                                  window_, reinterpret_cast<HMENU>(CloseWindowControl),
                                  instance_, nullptr);
  const std::array<HWND, 17> controls = {
      profilePicker_, labelEdit_, actionPicker_, actionValueEdit_,
      browseAction_, chooseIcon_, removeIcon_, colorEdit_,
      chooseColor_, saveButton_, cancelButton_, saveProfile_, pushLayout_,
      menuButton_, minimizeWindow_, maximizeWindow_, closeWindow_};
  for (HWND control : controls) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  }
  const std::array<const wchar_t*, 5> actions = {
      L"Do nothing", L"Launch application", L"Send keystrokes", L"Open URL",
      L"Run script"};
  for (const auto* action : actions) {
    SendMessageW(actionPicker_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(action));
  }
  RefreshProfiles();
  LoadDraft();
}

void MainWindow::PositionControls(int width, int height) {
  const int contentTop = 142;
  const int contentBottom = height - 132;
  const int contentHeight = std::max(180, contentBottom - contentTop);
  const int left = 28;
  const int leftWidth = std::max(400, static_cast<int>((width - 56) * 0.63));
  const int rightLeft = left + leftWidth + 24;
  const int rightWidth = std::max(260, width - rightLeft - 28);
  const int padding = 22;
  const int fieldLeft = rightLeft + padding;
  const int fieldWidth = rightWidth - padding * 2;
  int y = contentTop + 74;

  MoveWindow(profilePicker_, width - 500, 23, 210, 300, TRUE);
  MoveWindow(menuButton_, width - 280, 23, 40, 38, TRUE);
  MoveWindow(minimizeWindow_, width - 132, 22, 38, 34, TRUE);
  MoveWindow(maximizeWindow_, width - 90, 22, 38, 34, TRUE);
  MoveWindow(closeWindow_, width - 48, 22, 38, 34, TRUE);
  MoveWindow(labelEdit_, fieldLeft, y + 21, fieldWidth, 28, TRUE);
  y += 57;
  MoveWindow(chooseIcon_, fieldLeft, y + 21, (fieldWidth - 8) / 2, 28, TRUE);
  MoveWindow(removeIcon_, fieldLeft + (fieldWidth + 8) / 2, y + 21,
             (fieldWidth - 8) / 2, 28, TRUE);
  y += 84;
  MoveWindow(actionPicker_, fieldLeft, y, fieldWidth, 300, TRUE);
  y += 42;
  MoveWindow(actionValueEdit_, fieldLeft, y, fieldWidth - 88, 28, TRUE);
  MoveWindow(browseAction_, fieldLeft + fieldWidth - 82, y, 82, 28, TRUE);
  y += 60;
  MoveWindow(colorEdit_, fieldLeft, y, 90, 28, TRUE);
  MoveWindow(chooseColor_, fieldLeft + 100, y, 120, 28, TRUE);
  const int footerY = contentTop + contentHeight - 48;
  MoveWindow(cancelButton_, fieldLeft, footerY, (fieldWidth - 8) / 2, 32, TRUE);
  MoveWindow(saveButton_, fieldLeft + (fieldWidth + 8) / 2, footerY,
             (fieldWidth - 8) / 2, 32, TRUE);
  MoveWindow(saveProfile_, width - 320, height - 62, 120, 38, TRUE);
  MoveWindow(pushLayout_, width - 188, height - 62, 160, 38, TRUE);
  ShowWindow(browseAction_,
             SendMessageW(actionPicker_, CB_GETCURSEL, 0, 0) == 1 ||
                     SendMessageW(actionPicker_, CB_GETCURSEL, 0, 0) == 4
                 ? SW_SHOW
                 : SW_HIDE);
  SetWindowTextW(maximizeWindow_, IsZoomed(window_) ? L"❐" : L"□");
}

void MainWindow::Paint() {
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(window_, &paint);
  RECT client{};
  GetClientRect(window_, &client);
  Gdiplus::Graphics graphics(dc);
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.Clear(Gdiplus::Color(255, 8, 9, 11));
  Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 242, 245, 248));
  Gdiplus::SolidBrush mutedBrush(Gdiplus::Color(255, 151, 162, 175));
  Gdiplus::SolidBrush panelBrush(Gdiplus::Color(255, 17, 19, 23));
  Gdiplus::SolidBrush accentBrush(Gdiplus::Color(255, 139, 124, 255));
  Gdiplus::Pen borderPen(Gdiplus::Color(255, 48, 57, 67), 1.0f);
  Gdiplus::Font titleFont(L"Segoe UI", 20.0f, Gdiplus::FontStyleBold);
  Gdiplus::Font logoFont(L"Segoe UI", 15.0f, Gdiplus::FontStyleBold);
  Gdiplus::Font sectionFont(L"Segoe UI", 13.0f, Gdiplus::FontStyleBold);
  Gdiplus::Font labelFont(L"Segoe UI", 9.5f, Gdiplus::FontStyleBold);
  Gdiplus::Font smallFont(L"Segoe UI", 8.0f, Gdiplus::FontStyleBold);

  if (logoImage_) {
    Gdiplus::GraphicsPath logoMask;
    logoMask.AddArc(26, 20, 20, 20, 180, 90);
    logoMask.AddArc(60, 20, 20, 20, 270, 90);
    logoMask.AddArc(60, 54, 20, 20, 0, 90);
    logoMask.AddArc(26, 54, 20, 20, 90, 90);
    logoMask.CloseFigure();
    const Gdiplus::GraphicsState saved = graphics.Save();
    graphics.SetClip(&logoMask);
    graphics.DrawImage(logoImage_.get(), Gdiplus::Rect(26, 20, 54, 54));
    graphics.Restore(saved);
  } else {
    Gdiplus::RectF logoRect(28, 22, 50, 50);
    Gdiplus::GraphicsPath logoPath;
    logoPath.AddArc(28, 22, 50, 50, 0, 360);
    graphics.FillPath(&accentBrush, &logoPath);
    DrawText(graphics, L"C", logoFont, textBrush, logoRect,
             Gdiplus::StringAlignmentCenter);
  }
  DrawText(graphics, L"Crow", logoFont, textBrush,
           Gdiplus::RectF(92, 23, 160, 24), Gdiplus::StringAlignmentNear);
  DrawText(graphics, L"DESKTOP CONTROL DECK", smallFont, mutedBrush,
           Gdiplus::RectF(92, 48, 240, 18), Gdiplus::StringAlignmentNear);
  DrawText(graphics, collection_.currentProfile, titleFont, textBrush,
           Gdiplus::RectF(28, 81, 650, 38), Gdiplus::StringAlignmentNear);

  const int left = 28;
  const int top = 142;
  const int leftWidth = std::max(400, static_cast<int>((client.right - 56) * 0.63));
  const int gridHeight = std::max(180, static_cast<int>(client.bottom - 274));
  const int rightLeft = left + leftWidth + 24;
  const int rightWidth =
      std::max(260, static_cast<int>(client.right - rightLeft - 28));
  graphics.FillRectangle(&panelBrush, left, top, leftWidth, gridHeight);
  graphics.DrawRectangle(&borderPen, left, top, leftWidth, gridHeight);
  graphics.FillRectangle(&panelBrush, rightLeft, top, rightWidth, gridHeight);
  graphics.DrawRectangle(&borderPen, rightLeft, top, rightWidth, gridHeight);
  DrawText(graphics, L"Button layout", sectionFont, textBrush,
           Gdiplus::RectF(static_cast<float>(left + 20), static_cast<float>(top + 14),
                          220.0f, 26.0f),
           Gdiplus::StringAlignmentNear);
  DrawText(graphics, L"15 KEYS  ·  5 × 3", smallFont, mutedBrush,
           Gdiplus::RectF(static_cast<float>(left + leftWidth - 150),
                          static_cast<float>(top + 14), 130.0f, 26.0f),
           Gdiplus::StringAlignmentFar);
  PaintGrid(graphics, RECT{left + 16, top + 50, left + leftWidth - 16,
                           top + gridHeight - 16});
  DrawText(graphics, L"BUTTON SETTINGS", smallFont, mutedBrush,
           Gdiplus::RectF(static_cast<float>(rightLeft + 22),
                          static_cast<float>(top + 14), 220.0f, 18.0f),
           Gdiplus::StringAlignmentNear);
  wchar_t buttonName[32]{};
  swprintf_s(buttonName, L"Button %02d", selectedButton_ + 1);
  DrawText(graphics, buttonName, titleFont, textBrush,
           Gdiplus::RectF(static_cast<float>(rightLeft + 22),
                          static_cast<float>(top + 33), 240.0f, 33.0f),
           Gdiplus::StringAlignmentNear);
  int fieldY = top + 74;
  const auto field = [&](const wchar_t* name, int y) {
    DrawText(graphics, name, smallFont, mutedBrush,
             Gdiplus::RectF(static_cast<float>(rightLeft + 22),
                            static_cast<float>(y), 260.0f, 18.0f),
             Gdiplus::StringAlignmentNear);
  };
  field(L"BUTTON NAME  ·  APP ONLY", fieldY);
  field(L"BUTTON ARTWORK", fieldY + 57);
  field(L"WHAT SHOULD THIS BUTTON DO?", fieldY + 120);
  field(L"BUTTON COLOR", fieldY + 222);
  DrawText(graphics, L"Changes are staged until you save this button.",
           smallFont, mutedBrush,
           Gdiplus::RectF(static_cast<float>(rightLeft + 22),
                          static_cast<float>(top + gridHeight - 95),
                          static_cast<float>(rightWidth - 44), 40.0f),
           Gdiplus::StringAlignmentNear);
  DrawText(graphics, L"●", labelFont, deckOnline_ ? accentBrush : mutedBrush,
           Gdiplus::RectF(28, static_cast<float>(client.bottom - 59), 18, 28),
           Gdiplus::StringAlignmentNear);
  DrawText(graphics, statusText_, labelFont, mutedBrush,
           Gdiplus::RectF(48, static_cast<float>(client.bottom - 59),
                          static_cast<float>(client.right - 440), 28),
           Gdiplus::StringAlignmentNear);
  EndPaint(window_, &paint);
}

void MainWindow::DrawButton(const DRAWITEMSTRUCT& item) {
  const int id = static_cast<int>(item.CtlID);
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool hovered = (item.itemState & ODS_HOTLIGHT) != 0;
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  COLORREF fill = RGB(27, 30, 36);
  COLORREF text = RGB(232, 236, 242);
  COLORREF border = RGB(54, 60, 70);
  if (id == PushLayoutControl || id == SaveProfileControl) {
    fill = RGB(91, 78, 190);
    border = RGB(139, 124, 255);
  } else if (id == CloseWindowControl) {
    fill = RGB(58, 28, 34);
    border = RGB(94, 43, 51);
  } else if (id == MinimizeWindowControl || id == MaximizeWindowControl ||
             id == MenuButton) {
    fill = kBackground;
    border = kBackground;
  }
  if (hovered) {
    fill = id == PushLayoutControl || id == SaveProfileControl
               ? RGB(110, 96, 218)
               : (id == CloseWindowControl ? RGB(155, 48, 58)
                                           : RGB(42, 46, 54));
  }
  if (pressed) fill = RGB(GetRValue(fill) * 3 / 4,
                          GetGValue(fill) * 3 / 4,
                          GetBValue(fill) * 3 / 4);
  if (disabled) text = RGB(112, 118, 128);

  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
  HGDIOBJ oldPen = SelectObject(item.hDC, pen);
  const RECT bounds = item.rcItem;
  RoundRect(item.hDC, bounds.left, bounds.top, bounds.right, bounds.bottom,
            id == MinimizeWindowControl || id == MaximizeWindowControl ||
                    id == CloseWindowControl
                ? 8
                : 10,
            id == MinimizeWindowControl || id == MaximizeWindowControl ||
                    id == CloseWindowControl
                ? 8
                : 10);
  SelectObject(item.hDC, oldBrush);
  SelectObject(item.hDC, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
  SetBkMode(item.hDC, TRANSPARENT);
  SetTextColor(item.hDC, text);
  RECT textBounds = bounds;
  InflateRect(&textBounds, -6, -2);
  wchar_t label[128]{};
  GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
  HGDIOBJ oldFont = SelectObject(item.hDC, font_);
  DrawTextW(item.hDC, label, -1, &textBounds,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(item.hDC, oldFont);
  if (item.itemState & ODS_FOCUS) {
    InflateRect(&textBounds, 2, 1);
    DrawFocusRect(item.hDC, &textBounds);
  }
}

void MainWindow::PaintGrid(Gdiplus::Graphics& graphics, const RECT& bounds) {
  gridImages_.clear();
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  const auto active = std::find(collection_.profileNames.begin(),
                                collection_.profileNames.end(),
                                collection_.currentProfile);
  if (active == collection_.profileNames.end()) return;
  const Profile& profile = collection_.profiles[
      static_cast<size_t>(active - collection_.profileNames.begin())];
  const ThemeConfig& theme = profile.theme;
  const float scale = std::min(width / 800.0f, height / 480.0f);
  const float boardWidth = 800.0f * scale;
  const float boardHeight = 480.0f * scale;
  const float boardLeft = bounds.left + (width - boardWidth) / 2.0f;
  const float boardTop = bounds.top + (height - boardHeight) / 2.0f;
  const Gdiplus::RectF background(boardLeft, boardTop, boardWidth, boardHeight);
  if (theme.mode == BackgroundMode::Image && !theme.image.empty()) {
    const auto path = backgroundDirectory_ / theme.image;
    Gdiplus::Image image(path.c_str());
    if (image.GetLastStatus() == Gdiplus::Ok && image.GetWidth() > 0 &&
        image.GetHeight() > 0) {
      const float scale = std::max(
          boardWidth / static_cast<float>(image.GetWidth()),
          boardHeight / static_cast<float>(image.GetHeight()));
      const float imageWidth = image.GetWidth() * scale;
      const float imageHeight = image.GetHeight() * scale;
      graphics.DrawImage(&image, Gdiplus::RectF(
                                     boardLeft + (boardWidth - imageWidth) / 2.0f,
                                     boardTop + (boardHeight - imageHeight) / 2.0f,
                                     imageWidth, imageHeight));
    }
  } else if (theme.mode == BackgroundMode::Gradient) {
    Gdiplus::LinearGradientBrush gradient(
        background,
        Gdiplus::Color(255, GetRValue(ParseColor(theme.color1)),
                       GetGValue(ParseColor(theme.color1)),
                       GetBValue(ParseColor(theme.color1))),
        Gdiplus::Color(255, GetRValue(ParseColor(theme.color2)),
                       GetGValue(ParseColor(theme.color2)),
                       GetBValue(ParseColor(theme.color2))),
        Gdiplus::LinearGradientModeHorizontal);
    graphics.FillRectangle(&gradient, background);
  } else {
    Gdiplus::SolidBrush backgroundBrush(
        Gdiplus::Color(255, GetRValue(ParseColor(theme.color1)),
                       GetGValue(ParseColor(theme.color1)),
                       GetBValue(ParseColor(theme.color1))));
    graphics.FillRectangle(&backgroundBrush, background);
  }
  for (int index = 0; index < kButtonCount; ++index) {
    const int column = index % 5;
    const int row = index / 5;
    const ButtonConfig& button =
        index == selectedButton_ ? draft_ : profile.buttons[index];
    const float cellLeft = boardLeft + column * 160.0f * scale;
    const float cellTop = boardTop + row * 160.0f * scale;
    const float margin = 8.0f * scale;
    const Gdiplus::RectF tile(cellLeft + margin, cellTop + margin,
                              144.0f * scale, 144.0f * scale);
    const COLORREF baseColor =
        theme.perButton ? ParseColor(button.color) : RGB(23, 27, 34);
    Gdiplus::SolidBrush fill(Gdiplus::Color(
        255, GetRValue(baseColor), GetGValue(baseColor), GetBValue(baseColor)));
    Gdiplus::GraphicsPath rounded;
    const float radius = 12.0f * scale;
    rounded.AddArc(tile.X, tile.Y, radius * 2, radius * 2, 180, 90);
    rounded.AddArc(tile.GetRight() - radius * 2, tile.Y, radius * 2,
                   radius * 2, 270, 90);
    rounded.AddArc(tile.GetRight() - radius * 2, tile.GetBottom() - radius * 2,
                   radius * 2, radius * 2, 0, 90);
    rounded.AddArc(tile.X, tile.GetBottom() - radius * 2, radius * 2,
                   radius * 2, 90, 90);
    rounded.CloseFigure();
    graphics.FillPath(&fill, &rounded);
    if (!button.iconPath.empty() &&
        std::filesystem::exists(button.iconPath)) {
      auto image = std::make_unique<Gdiplus::Image>(button.iconPath.c_str());
      if (image->GetLastStatus() == Gdiplus::Ok &&
          image->GetWidth() > 0 && image->GetHeight() > 0) {
        const Gdiplus::GraphicsState saved = graphics.Save();
        graphics.SetClip(&rounded);
        graphics.DrawImage(image.get(), tile);
        graphics.Restore(saved);
      }
      gridImages_.push_back(std::move(image));
    }
    const COLORREF outlineColor =
        index == selectedButton_ ? kAccent : ParseColor(button.color);
    Gdiplus::Pen tilePen(Gdiplus::Color(
                             255, GetRValue(outlineColor), GetGValue(outlineColor),
                             GetBValue(outlineColor)),
                         index == selectedButton_ ? 2.0f : scale);
    if (theme.gridLines || index == selectedButton_) graphics.DrawPath(&tilePen, &rounded);
  }
}

void MainWindow::HandleCommand(int id, int notification) {
  if (populatingControls_) return;
  switch (id) {
    case ProfilePicker:
      if (notification == CBN_SELCHANGE) {
        const int selected = static_cast<int>(
            SendMessageW(profilePicker_, CB_GETCURSEL, 0, 0));
        ChangeProfile(selected);
      }
      break;
    case ActionPicker:
      if (notification == CBN_SELCHANGE) {
        draftDirty_ = true;
        UpdateActionFields();
      }
      break;
    case BrowseActionControl:
      BrowseAction();
      break;
    case ChooseIconControl:
      ChooseIcon();
      break;
    case RemoveIconControl:
      draft_.iconPath.clear();
      draftDirty_ = true;
      UpdateStateIndicators();
      InvalidateRect(window_, nullptr, TRUE);
      break;
    case ChooseColorControl:
      PickColor(&draft_.color);
      SetWindowTextW(colorEdit_, draft_.color.c_str());
      draftDirty_ = true;
      UpdateStateIndicators();
      InvalidateRect(window_, nullptr, TRUE);
      break;
    case SaveButtonControl:
      SaveDraft();
      break;
    case CancelButtonControl:
      CancelDraft();
      break;
    case SaveProfileControl:
      SaveProfile();
      break;
    case PushLayoutControl:
      PushLayout();
      break;
    case MenuButton:
      ShowProfileMenu();
      break;
    case MinimizeWindowControl:
      ShowWindow(window_, SW_MINIMIZE);
      break;
    case MaximizeWindowControl:
      ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
      break;
    case CloseWindowControl:
      SendMessageW(window_, WM_CLOSE, 0, 0);
      break;
    default:
      if (id == LabelEdit || id == ActionValueEdit || id == ColorEdit) {
        draftDirty_ = true;
        UpdateStateIndicators();
      }
  }
}

void MainWindow::SelectButton(int index) {
  if (index < 0 || index >= kButtonCount || index == selectedButton_) return;
  if (!ConfirmDraft()) return;
  selectedButton_ = index;
  LoadDraft();
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::LoadDraft() {
  const auto profile = std::find(collection_.profileNames.begin(),
                                 collection_.profileNames.end(),
                                 collection_.currentProfile);
  if (profile == collection_.profileNames.end()) return;
  const size_t index = static_cast<size_t>(profile - collection_.profileNames.begin());
  draft_ = collection_.profiles[index].buttons[selectedButton_];
  populatingControls_ = true;
  SetWindowTextW(labelEdit_, draft_.label.c_str());
  SetWindowTextW(colorEdit_, draft_.color.c_str());
  SendMessageW(actionPicker_, CB_SETCURSEL,
               static_cast<WPARAM>(draft_.action.type), 0);
  SetWindowTextW(actionValueEdit_, draft_.action.value.c_str());
  populatingControls_ = false;
  draftDirty_ = false;
  UpdateStateIndicators();
  UpdateActionFields();
  InvalidateRect(window_, nullptr, TRUE);
}

bool MainWindow::SaveDraft(bool showErrors) {
  draft_.label = GetWindowTextValue(labelEdit_);
  draft_.action.value = GetWindowTextValue(actionValueEdit_);
  draft_.color = GetWindowTextValue(colorEdit_);
  const int action = static_cast<int>(SendMessageW(actionPicker_, CB_GETCURSEL, 0, 0));
  draft_.action.type = action >= 0 && action <= 4
                           ? static_cast<ActionType>(action)
                           : ActionType::DoNothing;
  if (!IsColorValue(draft_.color)) {
    if (showErrors) MessageBoxW(window_, L"Choose a valid #RRGGBB button color.",
                                L"Invalid color", MB_OK | MB_ICONERROR);
    return false;
  }
  if (draft_.action.type == ActionType::OpenUrl) {
    const std::wstring& url = draft_.action.value;
    if (url.rfind(L"https://", 0) != 0 && url.rfind(L"http://", 0) != 0) {
      if (showErrors) MessageBoxW(window_, L"Enter a URL beginning with http:// or https://.",
                                  L"Invalid URL", MB_OK | MB_ICONERROR);
      return false;
    }
  }
  if (draft_.action.type == ActionType::LaunchApplication &&
      (draft_.action.value.size() < 5 ||
       _wcsicmp(draft_.action.value.substr(draft_.action.value.size() - 4).c_str(),
                L".exe") != 0 ||
       !std::filesystem::is_regular_file(draft_.action.value))) {
    if (showErrors) MessageBoxW(window_, L"Choose an .exe application.",
                                L"Invalid application", MB_OK | MB_ICONERROR);
    return false;
  }
  if (draft_.action.type == ActionType::RunScript) {
    std::wstring extension =
        std::filesystem::path(draft_.action.value).extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(towlower(value)); });
    if ((extension != L".py" && extension != L".bat") ||
        !std::filesystem::is_regular_file(draft_.action.value)) {
      if (showErrors) {
        MessageBoxW(window_, L"Choose an existing .py or .bat script file.",
                    L"Invalid script", MB_OK | MB_ICONERROR);
      }
      return false;
    }
  }
  const auto profile = std::find(collection_.profileNames.begin(),
                                 collection_.profileNames.end(),
                                 collection_.currentProfile);
  if (profile == collection_.profileNames.end()) return false;
  const size_t profileIndex =
      static_cast<size_t>(profile - collection_.profileNames.begin());
  collection_.profiles[profileIndex].buttons[selectedButton_] = draft_;
  draftDirty_ = false;
  MarkProfileDirty();
  InvalidateRect(window_, nullptr, TRUE);
  if (deck_.Connected()) {
    deck_.Queue(BuildButtonPacket(selectedButton_, draft_));
    deck_.Queue(AsciiPacket("REDRAW\n"));
    layoutSyncing_ = true;
    deckSynced_ = false;
    UpdateStateIndicators();
    SetStatus(L"Button updated · syncing to deck", true);
  }
  return true;
}

bool MainWindow::ConfirmDraft() {
  if (!draftDirty_) return true;
  const int response = MessageBoxW(
      window_, L"Save changes to this button before continuing?",
      L"Unsaved button changes", MB_YESNOCANCEL | MB_ICONQUESTION);
  if (response == IDCANCEL) return false;
  if (response == IDYES) return SaveDraft();
  CancelDraft();
  return true;
}

void MainWindow::CancelDraft() { LoadDraft(); }

void MainWindow::UpdateActionFields() {
  const int action = static_cast<int>(SendMessageW(actionPicker_, CB_GETCURSEL, 0, 0));
  const bool fileAction = action == 1 || action == 4;
  ShowWindow(actionValueEdit_, action == 0 ? SW_HIDE : SW_SHOW);
  ShowWindow(browseAction_, fileAction ? SW_SHOW : SW_HIDE);
  const wchar_t* placeholder = L"";
  if (action == 1) placeholder = L"Path to an .exe application";
  if (action == 2) placeholder = L"Example: Ctrl+Shift+M";
  if (action == 3) placeholder = L"https://example.com";
  if (action == 4) placeholder = L"Path to a .py or .bat script";
  SendMessageW(actionValueEdit_, EM_SETCUEBANNER, FALSE,
               reinterpret_cast<LPARAM>(placeholder));
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::RefreshProfiles() {
  populatingControls_ = true;
  SendMessageW(profilePicker_, CB_RESETCONTENT, 0, 0);
  for (const auto& name : collection_.profileNames) {
    SendMessageW(profilePicker_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(name.c_str()));
  }
  const auto active = std::find(collection_.profileNames.begin(),
                                collection_.profileNames.end(),
                                collection_.currentProfile);
  SendMessageW(profilePicker_, CB_SETCURSEL,
               active == collection_.profileNames.end()
                   ? 0
                   : active - collection_.profileNames.begin(),
               0);
  populatingControls_ = false;
}

void MainWindow::ChangeProfile(int index) {
  if (index < 0 || index >= static_cast<int>(collection_.profileNames.size())) return;
  if (!ConfirmDraft()) {
    RefreshProfiles();
    return;
  }
  collection_.currentProfile = collection_.profileNames[index];
  selectedButton_ = 0;
  MarkProfileDirty();
  LoadDraft();
  InvalidateRect(window_, nullptr, TRUE);
  QueueLayout();
}

void MainWindow::AddProfile() {
  if (!ConfirmDraft()) return;
  std::wstring name = L"New Profile";
  if (!PromptText(window_, instance_, L"New profile", &name)) return;
  if (std::find(collection_.profileNames.begin(), collection_.profileNames.end(),
                name) != collection_.profileNames.end()) {
    MessageBoxW(window_, L"A profile with that name already exists.",
                L"Duplicate profile name", MB_OK | MB_ICONERROR);
    return;
  }
  Profile profile = MakeDefaultProfile();
  collection_.profileNames.push_back(name);
  collection_.profiles.push_back(std::move(profile));
  collection_.currentProfile = name;
  selectedButton_ = 0;
  MarkProfileDirty();
  RefreshProfiles();
  LoadDraft();
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::RenameProfile() {
  if (!ConfirmDraft()) return;
  std::wstring name = collection_.currentProfile;
  if (!PromptText(window_, instance_, L"Rename profile", &name) ||
      name == collection_.currentProfile) {
    return;
  }
  if (std::find(collection_.profileNames.begin(), collection_.profileNames.end(),
                name) != collection_.profileNames.end()) {
    MessageBoxW(window_, L"A profile with that name already exists.",
                L"Duplicate profile name", MB_OK | MB_ICONERROR);
    return;
  }
  const auto found = std::find(collection_.profileNames.begin(),
                               collection_.profileNames.end(),
                               collection_.currentProfile);
  if (found == collection_.profileNames.end()) return;
  *found = name;
  collection_.currentProfile = std::move(name);
  MarkProfileDirty();
  RefreshProfiles();
  LoadDraft();
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::DeleteProfile() {
  if (!ConfirmDraft()) return;
  if (collection_.profileNames.size() <= 1) {
    MessageBoxW(window_, L"Crow needs at least one profile.",
                L"Delete profile", MB_OK | MB_ICONINFORMATION);
    return;
  }
  if (MessageBoxW(window_, L"Delete the current profile?", L"Delete profile",
                  MB_YESNO | MB_ICONWARNING) != IDYES) {
    return;
  }
  const auto found = std::find(collection_.profileNames.begin(),
                               collection_.profileNames.end(),
                               collection_.currentProfile);
  if (found == collection_.profileNames.end()) return;
  const size_t index = static_cast<size_t>(found - collection_.profileNames.begin());
  collection_.profileNames.erase(found);
  collection_.profiles.erase(collection_.profiles.begin() + index);
  collection_.currentProfile = collection_.profileNames.front();
  selectedButton_ = 0;
  MarkProfileDirty();
  RefreshProfiles();
  LoadDraft();
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::ShowProfileMenu() {
  HMENU menu = CreatePopupMenu();
  HMENU copyMenu = CreatePopupMenu();
  bool startupEnabled = false;
  std::wstring startupError;
  const bool startupSettingAvailable =
      GetStartupEnabled(&startupEnabled, &startupError);
  AppendMenuW(menu, MF_STRING, 1, L"New profile…");
  AppendMenuW(menu, MF_STRING, 2, L"Rename profile…");
  AppendMenuW(menu, MF_STRING, 3, L"Delete current profile");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 4, L"Deck background & grid…");
  AppendMenuW(menu, MF_STRING, 5, L"Settings & storage location");
  if (startupSettingAvailable) {
    AppendMenuW(menu, MF_STRING | (startupEnabled ? MF_CHECKED : 0),
                kStartupMenuCommand, L"Start Crow when I sign in");
  } else {
    AppendMenuW(menu, MF_STRING | MF_GRAYED, kStartupMenuCommand,
                L"Startup setting unavailable");
  }
  if (copyMenu) {
    const auto active = std::find(collection_.profileNames.begin(),
                                  collection_.profileNames.end(),
                                  collection_.currentProfile);
    if (active != collection_.profileNames.end()) {
      const size_t profileIndex =
          static_cast<size_t>(active - collection_.profileNames.begin());
      for (int target = 0; target < kButtonCount; ++target) {
        if (target == selectedButton_) continue;
        const std::wstring& name =
            collection_.profiles[profileIndex].buttons[target].label;
        std::wstring item = L"Button ";
        if (target + 1 < 10) item += L"0";
        item += std::to_wstring(target + 1);
        if (!name.empty()) item += L"  ·  " + name;
        AppendMenuW(copyMenu, MF_STRING, 200 + target, item.c_str());
      }
      AppendMenuW(menu, MF_POPUP,
                  reinterpret_cast<UINT_PTR>(copyMenu),
                  L"Copy selected button to…");
    } else {
      DestroyMenu(copyMenu);
      copyMenu = nullptr;
    }
  }
  POINT point{};
  GetCursorPos(&point);
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                      point.x, point.y, 0, window_, nullptr);
  DestroyMenu(menu);
  if (!startupSettingAvailable && !startupError.empty()) {
    MessageBoxW(window_, startupError.c_str(), L"Crow startup setting",
                MB_OK | MB_ICONERROR);
  }
  switch (command) {
    case 1:
      AddProfile();
      break;
    case 2:
      RenameProfile();
      break;
    case 3:
      DeleteProfile();
      break;
    case 4:
      EditBackground();
      break;
    case 5:
      ShowSettings();
      break;
    case kStartupMenuCommand:
      if (startupSettingAvailable) {
        std::wstring error;
        if (!SetStartupEnabled(!startupEnabled, &error)) {
          MessageBoxW(window_, error.c_str(), L"Could not update startup setting",
                      MB_OK | MB_ICONERROR);
        } else {
          SetStatus(!startupEnabled
                        ? L"Crow will start automatically when you sign in"
                        : L"Crow will no longer start automatically",
                    deckOnline_);
        }
      }
      break;
    default:
      if (command >= 200 && command < 200 + kButtonCount) {
        const int target = static_cast<int>(command) - 200;
        if (target == selectedButton_ || !ConfirmDraft()) break;
        const auto active = std::find(collection_.profileNames.begin(),
                                       collection_.profileNames.end(),
                                       collection_.currentProfile);
        if (active == collection_.profileNames.end()) break;
        const size_t profileIndex = static_cast<size_t>(
            active - collection_.profileNames.begin());
        collection_.profiles[profileIndex].buttons[target] =
            collection_.profiles[profileIndex].buttons[selectedButton_];
        MarkProfileDirty();
        if (deck_.Connected()) {
          try {
            deck_.Queue(BuildButtonPacket(
                target, collection_.profiles[profileIndex].buttons[target]));
            deck_.Queue(AsciiPacket("REDRAW\n"));
            layoutSyncing_ = true;
            deckSynced_ = false;
            UpdateStateIndicators();
            SetStatus(L"Button copied · syncing to deck", true);
          } catch (const std::exception& error) {
            SetStatus(L"Button copied but could not sync: " +
                          ProfileStore::Utf8ToWide(error.what()),
                      true);
          }
        } else {
          SetStatus(L"Button copied · save profile to keep changes", false);
        }
        InvalidateRect(window_, nullptr, TRUE);
      }
      break;
  }
}

bool MainWindow::ChooseImage(std::wstring* path, const wchar_t* title,
                             const wchar_t* filter) {
  wchar_t filename[32768]{};
  const bool initialDirectory =
      !path->empty() && std::filesystem::is_directory(*path);
  if (!path->empty() && !initialDirectory) wcscpy_s(filename, path->c_str());
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_;
  dialog.lpstrFilter = filter;
  dialog.lpstrFile = filename;
  dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
  dialog.lpstrTitle = title;
  if (initialDirectory) dialog.lpstrInitialDir = path->c_str();
  dialog.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_FILEMUSTEXIST |
                 OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return false;
  *path = filename;
  return true;
}

bool MainWindow::PickColor(std::wstring* color) {
  COLORREF custom[16]{};
  CHOOSECOLORW chooser{};
  chooser.lStructSize = sizeof(chooser);
  chooser.hwndOwner = window_;
  chooser.rgbResult = ParseColor(*color);
  chooser.lpCustColors = custom;
  chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
  if (!ChooseColorW(&chooser)) return false;
  *color = ColorString(chooser.rgbResult);
  return true;
}

void MainWindow::ChooseIcon() {
  std::wstring path;
  static constexpr wchar_t filter[] =
      L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0\0";
  if (!ChooseImage(&path, L"Choose button artwork", filter)) return;
  const std::filesystem::path imageDirectory =
      store_.Path().parent_path() / L"button-images";
  std::error_code fileError;
  std::filesystem::create_directories(imageDirectory, fileError);
  if (fileError) {
    MessageBoxW(window_, ProfileStore::Utf8ToWide(fileError.message()).c_str(),
                L"Could not prepare image storage", MB_OK | MB_ICONERROR);
    return;
  }
  uint64_t uniqueId = GetTickCount64();
  std::filesystem::path output;
  bool outputExists = false;
  do {
    output = imageDirectory /
             (L"button-" + std::to_wstring(selectedButton_ + 1) + L"-" +
              std::to_wstring(uniqueId++) + L".png");
    outputExists = std::filesystem::exists(output, fileError);
  } while (outputExists && !fileError);
  if (fileError) {
    MessageBoxW(window_, ProfileStore::Utf8ToWide(fileError.message()).c_str(),
                L"Could not prepare image storage", MB_OK | MB_ICONERROR);
    return;
  }
  std::wstring error;
  if (!CropImage(window_, instance_, path, output, &error)) {
    if (!error.empty()) {
      MessageBoxW(window_, error.c_str(), L"Could not use image",
                  MB_OK | MB_ICONERROR);
    }
    return;
  }
  auto image = std::make_unique<Gdiplus::Image>(output.c_str());
  if (image->GetLastStatus() != Gdiplus::Ok) {
    std::filesystem::remove(output, fileError);
    MessageBoxW(window_, L"The cropped image could not be loaded.",
                L"Could not use image", MB_OK | MB_ICONERROR);
    return;
  }
  draft_.iconPath = output.wstring();
  draftDirty_ = true;
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::BrowseAction() {
  const int action = static_cast<int>(SendMessageW(actionPicker_, CB_GETCURSEL, 0, 0));
  std::wstring path;
  static constexpr wchar_t exeFilter[] = L"Applications\0*.exe\0All files\0*.*\0\0";
  static constexpr wchar_t scriptFilter[] =
      L"Scripts\0*.py;*.bat\0Python files\0*.py\0Batch files\0*.bat\0\0";
  if (!ChooseImage(&path, action == 1 ? L"Choose an application" : L"Choose a script",
                   action == 1 ? exeFilter : scriptFilter)) {
    return;
  }
  SetWindowTextW(actionValueEdit_, path.c_str());
  draftDirty_ = true;
}

void MainWindow::EditBackground() {
  const auto profile = std::find(collection_.profileNames.begin(),
                                 collection_.profileNames.end(),
                                 collection_.currentProfile);
  if (profile == collection_.profileNames.end()) return;
  const size_t index = static_cast<size_t>(profile - collection_.profileNames.begin());
  ThemeConfig& theme = collection_.profiles[index].theme;
  ThemeConfig updated = theme;
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1, L"Solid color…");
  AppendMenuW(menu, MF_STRING, 2, L"Two-color gradient…");
  AppendMenuW(menu, MF_STRING, 3, L"Choose image…");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (theme.gridLines ? MF_CHECKED : 0), 4,
              L"Show button borders");
  AppendMenuW(menu, MF_STRING | (theme.perButton ? MF_CHECKED : 0), 5,
              L"Use per-button fills");
  POINT point{};
  GetCursorPos(&point);
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                      point.x, point.y, 0, window_, nullptr);
  DestroyMenu(menu);
  if (command == 1) {
    if (!PickColor(&updated.color1)) return;
    updated.mode = BackgroundMode::Solid;
  } else if (command == 2) {
    if (!PickColor(&updated.color1) || !PickColor(&updated.color2)) return;
    updated.mode = BackgroundMode::Gradient;
  } else if (command == 3) {
    std::wstring path = backgroundDirectory_.wstring();
    static constexpr wchar_t filter[] =
        L"Background images\0*.jpg;*.jpeg;*.png\0\0";
    if (!ChooseImage(&path, L"Choose a deck background", filter)) return;
    std::filesystem::create_directories(backgroundDirectory_);
    const auto destination = backgroundDirectory_ / std::filesystem::path(path).filename();
    std::error_code error;
    if (std::filesystem::absolute(path) != std::filesystem::absolute(destination)) {
      std::filesystem::copy_file(
          path, destination, std::filesystem::copy_options::overwrite_existing, error);
      if (error) {
        MessageBoxW(window_, ProfileStore::Utf8ToWide(error.message()).c_str(),
                    L"Could not add image",
                    MB_OK | MB_ICONERROR);
        return;
      }
    }
    updated.image = destination.filename().wstring();
    updated.mode = BackgroundMode::Image;
  } else if (command == 4) {
    updated.gridLines = !updated.gridLines;
  } else if (command == 5) {
    updated.perButton = !updated.perButton;
  } else {
    return;
  }
  theme = std::move(updated);
  MarkProfileDirty();
  InvalidateRect(window_, nullptr, TRUE);
  QueueLayout();
}

void MainWindow::SaveProfile() {
  if (!ConfirmDraft()) return;
  std::wstring error;
  if (!store_.Save(collection_, &error)) {
    MessageBoxW(window_, error.c_str(), L"Could not save profile",
                MB_OK | MB_ICONERROR);
    return;
  }
  profileDirty_ = false;
  UpdateStateIndicators();
  SetStatus(deckSynced_ ? L"Profile saved · deck synced"
                        : L"Profile saved on this PC",
            deck_.Connected());
}

void MainWindow::PushLayout() {
  if (!ConfirmDraft()) return;
  if (!deck_.Connected()) {
    MessageBoxW(window_, L"Connect the Elecrow over USB and wait until it is online.",
                L"Deck not connected", MB_OK | MB_ICONWARNING);
    return;
  }
  QueueLayout();
}

void MainWindow::QueueLayout() {
  if (!deck_.Connected()) return;
  try {
    for (auto& packet : BuildLayoutPackets()) deck_.Queue(std::move(packet));
    layoutSyncing_ = true;
    deckSynced_ = false;
    UpdateStateIndicators();
    SetStatus(profileDirty_ ? L"Syncing layout · profile not saved"
                            : L"Syncing layout to deck…",
              true);
  } catch (const std::exception& error) {
    layoutSyncing_ = false;
    deckSynced_ = false;
    UpdateStateIndicators();
    SetStatus(L"Could not prepare layout: " +
                  ProfileStore::Utf8ToWide(error.what()),
              false);
  }
}

std::vector<std::vector<uint8_t>> MainWindow::BuildLayoutPackets() {
  const auto profile = std::find(collection_.profileNames.begin(),
                                 collection_.profileNames.end(),
                                 collection_.currentProfile);
  if (profile == collection_.profileNames.end()) {
    throw std::runtime_error("No active profile is selected.");
  }
  const Profile& selected =
      collection_.profiles[static_cast<size_t>(profile - collection_.profileNames.begin())];
  const ThemeConfig& theme = selected.theme;
  std::vector<std::vector<uint8_t>> packets;
  const std::string themeLine =
      "THEME|" + ProfileStore::WideToUtf8(BackgroundModeName(theme.mode)) + "|" +
      ProfileStore::WideToUtf8(theme.color1) + "|" +
      ProfileStore::WideToUtf8(theme.color2) + "|" +
      (theme.gridLines ? "1" : "0") + "|" + (theme.perButton ? "1" : "0") +
      "\n";
  packets.push_back(AsciiPacket(themeLine));
  if (theme.mode == BackgroundMode::Image) {
    const auto path = backgroundDirectory_ / theme.image;
    auto jpeg = EncodeBackgroundJpeg(path);
    const std::string header = "BGJPG|" + std::to_string(jpeg.size()) + "\n";
    std::vector<uint8_t> packet(header.begin(), header.end());
    packet.insert(packet.end(), jpeg.begin(), jpeg.end());
    packets.push_back(std::move(packet));
  }
  for (int index = 0; index < kButtonCount; ++index) {
    packets.push_back(BuildButtonPacket(index, selected.buttons[index]));
  }
  packets.push_back(AsciiPacket("REDRAW\n"));
  return packets;
}

std::vector<uint8_t> MainWindow::BuildButtonPacket(
    int index, const ButtonConfig& button) {
  std::vector<uint8_t> packets;
  if (!button.iconPath.empty()) {
    std::ifstream file(std::filesystem::path(button.iconPath), std::ios::binary);
    if (!file) throw std::runtime_error("Could not open button PNG image.");
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (image.empty() || image.size() > 65536) {
      throw std::runtime_error("Button PNG files must be under 64 KB.");
    }
    const std::string header = "ICONPNG|" + std::to_string(index + 1) + "|" +
                               std::to_string(image.size()) + "\n";
    packets.insert(packets.end(), header.begin(), header.end());
    packets.insert(packets.end(), image.begin(), image.end());
  } else {
    const std::string clear = "ICONCLEAR|" + std::to_string(index + 1) + "\n";
    packets.insert(packets.end(), clear.begin(), clear.end());
  }
  const std::string line =
      "BUTTON|" + std::to_string(index + 1) + "|" +
      ProfileStore::WideToUtf8(button.color) + "|" + UrlEncode(button.label) +
      "|" + UrlEncode(button.icon) + "\n";
  packets.insert(packets.end(), line.begin(), line.end());
  return packets;
}

std::vector<uint8_t> MainWindow::EncodeBackgroundJpeg(
    const std::filesystem::path& path) {
  Gdiplus::Image source(path.c_str());
  if (source.GetLastStatus() != Gdiplus::Ok) {
    throw std::runtime_error("Could not read the selected background image.");
  }
  Gdiplus::Bitmap fitted(800, 480, PixelFormat24bppRGB);
  Gdiplus::Graphics graphics(&fitted);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  const float scale = std::max(800.0f / source.GetWidth(),
                               480.0f / source.GetHeight());
  const float width = source.GetWidth() * scale;
  const float height = source.GetHeight() * scale;
  graphics.DrawImage(&source, (800.0f - width) / 2.0f,
                     (480.0f - height) / 2.0f, width, height);
  IStream* stream = nullptr;
  if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK) {
    throw std::runtime_error("Could not prepare the background image stream.");
  }
  CLSID encoder{};
  if (GetEncoderClsid(L"image/jpeg", &encoder) < 0 ||
      fitted.Save(stream, &encoder, nullptr) != Gdiplus::Ok) {
    stream->Release();
    throw std::runtime_error("Could not encode the background as JPEG.");
  }
  HGLOBAL memory = nullptr;
  if (GetHGlobalFromStream(stream, &memory) != S_OK) {
    stream->Release();
    throw std::runtime_error("Could not read the encoded background image.");
  }
  STATSTG statistics{};
  if (stream->Stat(&statistics, STATFLAG_NONAME) != S_OK ||
      statistics.cbSize.QuadPart == 0) {
    stream->Release();
    throw std::runtime_error("Could not determine the encoded background size.");
  }
  const ULONGLONG streamSize = statistics.cbSize.QuadPart;
  const SIZE_T allocationSize = GlobalSize(memory);
  const auto* bytes = static_cast<const uint8_t*>(GlobalLock(memory));
  if (!bytes || streamSize > allocationSize || streamSize > 700000) {
    if (bytes) GlobalUnlock(memory);
    stream->Release();
    throw std::runtime_error("Fitted background JPEG must be 700 KB or smaller.");
  }
  std::vector<uint8_t> result(bytes, bytes + static_cast<size_t>(streamSize));
  GlobalUnlock(memory);
  stream->Release();
  return result;
}

void MainWindow::PollDeck() {
  DeckEvent event;
  bool redraw = false;
  while (deck_.TryPopEvent(&event)) {
    switch (event.type) {
      case DeckEvent::Type::Connected:
        deckOnline_ = true;
        deckSynced_ = false;
        layoutSyncing_ = false;
        SetStatus(L"Deck connected · " + event.text, true);
        QueueLayout();
        break;
      case DeckEvent::Type::Disconnected:
        deckOnline_ = false;
        deckSynced_ = false;
        layoutSyncing_ = false;
        SetStatus(L"Deck disconnected · retrying", false);
        break;
      case DeckEvent::Type::Touch: {
        wchar_t status[160]{};
        const auto profile = std::find(collection_.profileNames.begin(),
                                       collection_.profileNames.end(),
                                       collection_.currentProfile);
        std::wstring label;
        if (profile != collection_.profileNames.end()) {
          const size_t index = profile - collection_.profileNames.begin();
          label = collection_.profiles[index].buttons[event.button - 1].label;
        }
        swprintf_s(status, L"Button %d %s%s%ls", event.button,
                   event.pressed ? L"pressed" : L"released",
                   label.empty() ? L"" : L" · ", label.c_str());
        SetStatus(status, true);
        if (event.pressed && event.button >= 1 && event.button <= kButtonCount &&
            profile != collection_.profileNames.end()) {
          std::wstring error;
          const size_t index = profile - collection_.profileNames.begin();
          const ButtonConfig& button =
              collection_.profiles[index].buttons[event.button - 1];
          if (!ActionExecutor::Execute(button, &error)) {
            SetStatus(L"Button " + std::to_wstring(event.button) +
                          L" action failed: " + error,
                      true);
            MessageBoxW(window_, error.c_str(), L"Button action failed",
                        MB_OK | MB_ICONERROR);
          }
        }
        break;
      }
      case DeckEvent::Type::Synced:
        layoutSyncing_ = false;
        deckSynced_ = true;
        SetStatus(profileDirty_ ? L"Deck synced · profile not saved"
                                : L"Profile saved · deck synced",
                  true);
        break;
      case DeckEvent::Type::Error:
        layoutSyncing_ = false;
        deckSynced_ = false;
        SetStatus(event.text, false);
        break;
    }
    redraw = true;
  }
  if (redraw) InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::SetStatus(const std::wstring& text, bool online) {
  statusText_ = text;
  deckOnline_ = online;
  UpdateStateIndicators();
  InvalidateRect(window_, nullptr, TRUE);
}

void MainWindow::MarkProfileDirty() {
  profileDirty_ = true;
  layoutSyncing_ = false;
  deckSynced_ = false;
  UpdateStateIndicators();
  SetStatus(deckOnline_ ? L"Profile has unsaved changes · sync when ready"
                        : L"Profile has unsaved changes",
            deckOnline_);
}

void MainWindow::UpdateStateIndicators() {
  if (saveProfile_) {
    SetWindowTextW(saveProfile_,
                   profileDirty_ || draftDirty_ ? L"Save changes"
                                                : L"Profile saved");
  }
  if (pushLayout_) {
    const wchar_t* text = L"Push to deck  →";
    if (layoutSyncing_) {
      text = L"Syncing…";
    } else if (deckSynced_) {
      text = L"Synced  ✓";
    } else if (profileDirty_ || draftDirty_) {
      text = L"Sync changes  →";
    }
    SetWindowTextW(pushLayout_, text);
  }
}

void MainWindow::ShowSettings() {
  const std::wstring settings = L"Profiles: " + store_.Path().wstring() +
      L"\n\nBackground assets: " + backgroundDirectory_.wstring() +
      L"\n\nDeck connection: USB serial at 115200 baud.";
  MessageBoxW(window_, settings.c_str(), L"Crow settings",
              MB_OK | MB_ICONINFORMATION);
}

bool MainWindow::GetStartupEnabled(bool* enabled, std::wstring* error) const {
  HKEY key = nullptr;
  const LSTATUS openResult = RegOpenKeyExW(
      HKEY_CURRENT_USER, kStartupRegistryPath, 0, KEY_QUERY_VALUE, &key);
  if (openResult == ERROR_FILE_NOT_FOUND) {
    *enabled = false;
    return true;
  }
  if (openResult != ERROR_SUCCESS) {
    *error = L"Could not read Crow's Windows startup setting (error " +
             std::to_wstring(openResult) + L").";
    return false;
  }

  DWORD type = 0;
  DWORD size = 0;
  LSTATUS result = RegQueryValueExW(key, kStartupRegistryValue, nullptr, &type,
                                    nullptr, &size);
  if (result == ERROR_FILE_NOT_FOUND) {
    RegCloseKey(key);
    *enabled = false;
    return true;
  }
  if (result != ERROR_SUCCESS || type != REG_SZ || size < sizeof(wchar_t)) {
    RegCloseKey(key);
    *error = L"Could not read Crow's Windows startup setting (error " +
             std::to_wstring(result == ERROR_SUCCESS ? ERROR_INVALID_DATA
                                                     : result) +
             L").";
    return false;
  }

  std::vector<wchar_t> command(size / sizeof(wchar_t) + 1, L'\0');
  result = RegQueryValueExW(key, kStartupRegistryValue, nullptr, &type,
                            reinterpret_cast<BYTE*>(command.data()), &size);
  RegCloseKey(key);
  if (result != ERROR_SUCCESS || type != REG_SZ) {
    *error = L"Could not read Crow's Windows startup setting (error " +
             std::to_wstring(result == ERROR_SUCCESS ? ERROR_INVALID_DATA
                                                     : result) +
             L").";
    return false;
  }
  *enabled = command.front() != L'\0';
  return true;
}

bool MainWindow::SetStartupEnabled(bool enabled, std::wstring* error) const {
  HKEY key = nullptr;
  DWORD disposition = 0;
  const LSTATUS openResult = RegCreateKeyExW(
      HKEY_CURRENT_USER, kStartupRegistryPath, 0, nullptr, 0, KEY_SET_VALUE,
      nullptr, &key, &disposition);
  if (openResult != ERROR_SUCCESS) {
    *error = L"Could not update Crow's Windows startup setting (error " +
             std::to_wstring(openResult) + L").";
    return false;
  }

  LSTATUS result = ERROR_SUCCESS;
  if (enabled) {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
      RegCloseKey(key);
      *error = L"Could not determine Crow's executable path.";
      return false;
    }
    executable.resize(length);
    const std::wstring command = L"\"" + executable + L"\" --background";
    result = RegSetValueExW(
        key, kStartupRegistryValue, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
  } else {
    result = RegDeleteValueW(key, kStartupRegistryValue);
    if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  if (result != ERROR_SUCCESS) {
    *error = L"Could not update Crow's Windows startup setting (error " +
             std::to_wstring(result) + L").";
    return false;
  }
  return true;
}

}  // namespace crowdeck
