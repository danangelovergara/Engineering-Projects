#include "crowdeck/DeckSerial.h"

#include "crowdeck/ProfileStore.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <stdexcept>

namespace crowdeck {

DeckSerial::~DeckSerial() { Stop(); }

void DeckSerial::Start() {
  if (worker_.joinable()) return;
  stop_ = false;
  worker_ = std::thread(&DeckSerial::Run, this);
}

void DeckSerial::Stop() {
  stop_ = true;
  commandCondition_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void DeckSerial::Queue(std::vector<uint8_t> bytes) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    commands_.push(std::move(bytes));
  }
  commandCondition_.notify_one();
}

bool DeckSerial::TryPopEvent(DeckEvent* event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (events_.empty()) return false;
  *event = std::move(events_.front());
  events_.pop();
  return true;
}

bool DeckSerial::Connected() const { return connected_.load(); }

void DeckSerial::Emit(DeckEvent event) {
  std::lock_guard<std::mutex> lock(mutex_);
  events_.push(std::move(event));
}

std::vector<std::wstring> DeckSerial::EnumeratePorts() const {
  std::vector<std::wstring> ports;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ,
                    &key) != ERROR_SUCCESS) {
    return ports;
  }
  for (DWORD index = 0;; ++index) {
    std::array<wchar_t, 256> valueName{};
    std::array<wchar_t, 256> valueData{};
    DWORD nameSize = static_cast<DWORD>(valueName.size());
    DWORD dataSize = static_cast<DWORD>(valueData.size() * sizeof(wchar_t));
    DWORD type = 0;
    const LONG result = RegEnumValueW(key, index, valueName.data(), &nameSize,
                                     nullptr, &type,
                                     reinterpret_cast<BYTE*>(valueData.data()),
                                     &dataSize);
    if (result == ERROR_NO_MORE_ITEMS) break;
    if (result == ERROR_SUCCESS && type == REG_SZ) {
      std::wstring name(valueData.data());
      if (std::find(ports.begin(), ports.end(), name) == ports.end()) {
        ports.push_back(std::move(name));
      }
    }
  }
  RegCloseKey(key);
  std::sort(ports.begin(), ports.end());
  return ports;
}

HANDLE DeckSerial::OpenPort(const std::wstring& name) const {
  const std::wstring devicePath = L"\\\\.\\" + name;
  HANDLE port = CreateFileW(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (port == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

  DCB settings{};
  settings.DCBlength = sizeof(settings);
  if (!GetCommState(port, &settings)) {
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }
  settings.BaudRate = CBR_115200;
  settings.ByteSize = 8;
  settings.Parity = NOPARITY;
  settings.StopBits = ONESTOPBIT;
  settings.fDtrControl = DTR_CONTROL_ENABLE;
  settings.fRtsControl = RTS_CONTROL_ENABLE;
  if (!SetCommState(port, &settings)) {
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }
  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutConstant = 30;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 2000;
  timeouts.WriteTotalTimeoutMultiplier = 100;
  if (!SetCommTimeouts(port, &timeouts) || !SetupComm(port, 8192, 8192)) {
    CloseHandle(port);
    return INVALID_HANDLE_VALUE;
  }
  PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
  return port;
}

bool DeckSerial::ProbePort(HANDLE port, const std::wstring& name) {
  const auto deadline = GetTickCount64() + 2500;
  std::string buffer;
  while (!stop_ && GetTickCount64() < deadline) {
    DWORD written = 0;
    static constexpr char handshake[] = "HANDSHAKE\n";
    WriteFile(port, handshake, sizeof(handshake) - 1, &written, nullptr);
    const auto readDeadline = GetTickCount64() + 350;
    while (!stop_ && GetTickCount64() < readDeadline) {
      char bytes[256];
      DWORD received = 0;
      if (!ReadFile(port, bytes, sizeof(bytes), &received, nullptr)) return false;
      for (DWORD index = 0; index < received; ++index) {
        if (bytes[index] == '\r') continue;
        if (bytes[index] == '\n') {
          if (buffer.rfind("READY|CROWPANEL|", 0) == 0) {
            portName_ = name;
            return true;
          }
          buffer.clear();
        } else if (buffer.size() < 256) {
          buffer.push_back(bytes[index]);
        } else {
          buffer.clear();
        }
      }
      Sleep(15);
    }
  }
  return false;
}

bool DeckSerial::WritePacket(HANDLE port, const std::vector<uint8_t>& packet) {
  size_t offset = 0;
  while (offset < packet.size() && !stop_) {
    const DWORD requested = static_cast<DWORD>(
        std::min<size_t>(4096, packet.size() - offset));
    DWORD written = 0;
    if (!WriteFile(port, packet.data() + offset, requested, &written, nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return offset == packet.size();
}

void DeckSerial::ReadLines(HANDLE port, std::string* partial) {
  char bytes[512];
  DWORD received = 0;
  if (!ReadFile(port, bytes, sizeof(bytes), &received, nullptr)) {
    throw std::runtime_error("Serial read failed.");
  }
  for (DWORD index = 0; index < received; ++index) {
    const char value = bytes[index];
    if (value == '\r') continue;
    if (value == '\n') {
      if (partial->rfind("READY|CROWPANEL|", 0) == 0) {
        Emit({DeckEvent::Type::Connected, portName_});
      } else if (*partial == "SYNCED") {
        Emit({DeckEvent::Type::Synced, L"", 0, false});
      } else if (partial->rfind("ERROR|", 0) == 0) {
        Emit({DeckEvent::Type::Error,
              ProfileStore::Utf8ToWide(partial->substr(6))});
      } else if (partial->rfind("TOUCH|button=", 0) == 0) {
        const size_t separator = partial->find('|', 13);
        if (separator != std::string::npos) {
          try {
            const int number = std::stoi(partial->substr(13, separator - 13));
            const std::string state = partial->substr(separator + 1);
            if (number >= 1 && number <= 15 &&
                (state == "pressed" || state == "released")) {
              DeckEvent event;
              event.type = DeckEvent::Type::Touch;
              event.button = number;
              event.pressed = state == "pressed";
              Emit(std::move(event));
            }
          } catch (...) {
          }
        }
      }
      partial->clear();
    } else if (partial->size() < 512) {
      partial->push_back(value);
    } else {
      partial->clear();
    }
  }
}

void DeckSerial::Run() {
  while (!stop_) {
    if (!connected_) {
      for (const auto& name : EnumeratePorts()) {
        if (stop_) break;
        HANDLE port = OpenPort(name);
        if (port == INVALID_HANDLE_VALUE) continue;
        if (ProbePort(port, name)) {
          connected_ = true;
          Emit({DeckEvent::Type::Connected, name});
          std::string partial;
          try {
            while (!stop_ && connected_) {
              std::vector<uint8_t> command;
              {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!commands_.empty()) {
                  command = std::move(commands_.front());
                  commands_.pop();
                }
              }
              if (!command.empty() && !WritePacket(port, command)) {
                throw std::runtime_error("Serial write failed.");
              }
              ReadLines(port, &partial);
              Sleep(8);
            }
          } catch (const std::exception& error) {
            if (!stop_) {
              Emit({DeckEvent::Type::Error,
                    ProfileStore::Utf8ToWide(error.what())});
            }
          }
          CloseHandle(port);
          connected_ = false;
          portName_.clear();
          if (!stop_) Emit({DeckEvent::Type::Disconnected, L"", 0, false});
          break;
        }
        CloseHandle(port);
      }
      if (!connected_ && !stop_) Sleep(1500);
    }
  }
}

}  // namespace crowdeck
