#pragma once

#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace crowdeck {

struct DeckEvent {
  enum class Type { Connected, Disconnected, Touch, Synced, Error };
  Type type = Type::Disconnected;
  std::wstring text;
  int button = 0;
  bool pressed = false;
};

class DeckSerial {
 public:
  DeckSerial() = default;
  ~DeckSerial();
  DeckSerial(const DeckSerial&) = delete;
  DeckSerial& operator=(const DeckSerial&) = delete;

  void Start();
  void Stop();
  void Queue(std::vector<uint8_t> bytes);
  bool TryPopEvent(DeckEvent* event);
  bool Connected() const;

 private:
  void Run();
  std::vector<std::wstring> EnumeratePorts() const;
  HANDLE OpenPort(const std::wstring& name) const;
  bool ProbePort(HANDLE port, const std::wstring& name);
  bool WritePacket(HANDLE port, const std::vector<uint8_t>& packet);
  void ReadLines(HANDLE port, std::string* partial);
  void Emit(DeckEvent event);

  std::atomic<bool> stop_{false};
  std::atomic<bool> connected_{false};
  std::thread worker_;
  mutable std::mutex mutex_;
  std::condition_variable commandCondition_;
  std::queue<std::vector<uint8_t>> commands_;
  std::queue<DeckEvent> events_;
  std::wstring portName_;
};

}  // namespace crowdeck
