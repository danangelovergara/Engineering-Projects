#pragma once

#include "crowdeck/Models.h"

#include <filesystem>
#include <string>

namespace crowdeck {

class ProfileStore {
 public:
  explicit ProfileStore(std::filesystem::path path);

  const std::filesystem::path& Path() const;
  ProfileCollection Load(std::wstring* warning) const;
  bool Save(const ProfileCollection& collection, std::wstring* error) const;

  static std::filesystem::path DefaultPath();
  static std::wstring Utf8ToWide(const std::string& value);
  static std::string WideToUtf8(const std::wstring& value);

 private:
  std::filesystem::path path_;
};

}  // namespace crowdeck
