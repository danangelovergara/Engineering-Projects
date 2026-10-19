@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
where g++ >nul 2>nul
if errorlevel 1 (
  echo A MinGW-w64 g++ compiler is required. Install MSYS2 UCRT64 and add its bin folder to PATH.
  exit /b 1
)
if not exist build mkdir build
set SOURCES=
for %%F in (src\*.cpp) do set SOURCES=!SOURCES! "%%F"
windres crow.rc -O coff -o build\crow-res.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -Wall -Wextra -Wpedantic -DUNICODE -D_UNICODE -municode -mwindows -Iinclude !SOURCES! build\crow-res.o -o build\Crow.exe -lgdiplus -lcomctl32 -lcomdlg32 -ldwmapi -lole32 -ladvapi32 -lshell32
if errorlevel 1 exit /b 1
echo Built build\Crow.exe
