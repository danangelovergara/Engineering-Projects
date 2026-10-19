cmake_minimum_required(VERSION 3.20)
project(Crow LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The Crow desktop companion currently requires Windows.")
endif()

enable_language(RC)
configure_file(crow.ico ${CMAKE_CURRENT_BINARY_DIR}/crow.ico COPYONLY)

add_executable(Crow WIN32
  crow.rc
  src/ActionExecutor.cpp
  src/DeckSerial.cpp
  src/Json.cpp
  src/MainWindow.cpp
  src/Models.cpp
  src/ProfileStore.cpp
  src/main.cpp
)

target_include_directories(Crow PRIVATE include)
target_compile_features(Crow PRIVATE cxx_std_17)
target_compile_definitions(Crow PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(Crow PRIVATE
  gdiplus
  comctl32
  comdlg32
  dwmapi
  ole32
  advapi32
  shell32
)

if(MINGW)
  target_link_options(Crow PRIVATE -municode)
  target_compile_options(Crow PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
  target_compile_options(Crow PRIVATE /W4 /permissive-)
endif()
