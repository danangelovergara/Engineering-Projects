# Crow

Crow is a Windows desktop companion for the Elecrow CrowPanel ESP32-S3 7-inch display. The PC owns profiles, button configuration, and actions; the board renders the layout and reports touch events over USB serial.

## Build the Windows companion

Install a C++17 Windows compiler. The included batch file builds with MinGW-w64:

```powershell
.\desktop\build.bat
.\desktop\build\Crow.exe
```

Alternatively, configure and build the native project with CMake:

```powershell
cmake -S desktop -B desktop\build
cmake --build desktop\build --config Release
```

The crow artwork is embedded as a multi-size Windows icon for `Crow.exe` and used by the window and system-tray icon. Crow also uses it in the app header. The borderless, resizable window stores profiles in `%APPDATA%\Crow\profiles.json` with cropped button artwork in `%APPDATA%\Crow\button-images\`. Enable **Start Crow when I sign in** in the (`⋯`) menu to optionally launch Crow in the system tray when your Windows account signs in; choose it again to disable startup. This setting applies only to the current Windows user. Closing the window hides Crow in the Windows notification area (system tray), so it can keep handling deck button presses without occupying the taskbar. Use the tray icon's **Open Crow** command to reopen the window or **Exit Crow** to quit completely. Existing profiles from `%APPDATA%\CrowDeck\profiles.json` are loaded automatically; saving writes them to the new location while leaving the original file as a backup. The menu (`⋯`) lets you create, rename, or delete profiles, copy one button's settings to another slot, and adjust the deck background and grid. Select a key to edit its app-only name, artwork, action, and color. **Choose image…** accepts common image formats; drag to position and scroll to zoom, then use the preview to create a deck-ready 100×100 PNG. Transparent pixels are preserved. If an image has a solid backdrop, enable **Remove edge background** to make edge-connected background colors transparent while retaining separate details such as white logo marks. The finished PNG must fit within the deck's 64 KB transfer limit. The artwork fills the button face on both the desktop preview and LCD; the LCD does not overlay button numbers, names, or built-in symbols. The **Save profile** and **Push to deck** buttons show whether edits are unsaved, syncing, or synced.

Actions run on the PC when the matching physical button is pressed:

- Launch an `.exe` application.
- Send a key or chord such as `Ctrl+Shift+M`.
- Open an `http://` or `https://` URL in the default browser.
- Run a `.py` script using the Windows `py.exe` launcher or a `.bat` script using `cmd.exe`.
- Leave the button empty with **Do nothing**.

Use **Deck background & grid…** to select a solid color, two-color gradient, or image and to toggle borders and per-button fills. Background images selected in the app are copied to `assets\backgrounds\`; images are fitted to the board's 800×480 screen and transferred as JPEG.

## Build and upload the ESP32 firmware

Install the Microsoft C/C++ and PlatformIO VS Code extensions, open this project, then use PlatformIO's Build and Upload commands. The firmware uses LVGL 8.4, LovyanGFX, and TAMC_GT911 as declared in `platformio.ini`. Close any serial monitor before opening the companion app.

Firmware responsibilities are split by concern:

- `src\main.cpp` coordinates startup and the main loop.
- `src\deck_state.h` defines the display/layout state shared by firmware modules.
- `src\deck_render.cpp` draws the background and 5×3 button grid.
- `src\deck_protocol.cpp` parses USB serial commands and receives images.
- `src\deck_touch.cpp` maps touches to button press/release events.
- `src\lgfx.cpp` and `include\lgfx.h` contain display and touch hardware setup.

The USB protocol runs at 115200 baud. The PC scans serial ports for `READY|CROWPANEL|1.0.0`, then sends the profile using `THEME`, `BUTTON`, `ICONPNG`, `ICONCLEAR`, and `BGJPG` messages. The board reports `TOUCH|button=N|pressed` / `released` and `SYNCED` after drawing the layout.

## Privacy and local data

Crow keeps profiles and cropped button images in the current Windows user's `%APPDATA%\Crow` folder. Deck background images chosen in the app are copied into `assets\backgrounds\` in the project folder; Git ignores files in that folder so personal images are not added accidentally. Crow communicates with the panel over USB serial and runs only the applications, scripts, URLs, or keystrokes configured for pressed buttons. Review your button actions and check the staged Git file list before publishing; never commit personal profiles, images, credentials, or generated build files.

Build and upload the firmware to your CrowPanel before using the desktop app with the physical deck.


