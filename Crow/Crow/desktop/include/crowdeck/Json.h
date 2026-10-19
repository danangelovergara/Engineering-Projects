#pragma once

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace crowdeck::json {

struct Value {
  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data =
      nullptr;

  Value() = default;
  Value(bool value) : data(value) {}
  Value(double value) : data(value) {}
  Value(std::string value) : data(std::move(value)) {}
  Value(const char* value) : data(std::string(value)) {}
  Value(Array value) : data(std::move(value)) {}
  Value(Object value) : data(std::move(value)) {}

  const Object* AsObject() const;
  const Array* AsArray() const;
  const std::string* AsString() const;
  const Value* Find(const std::string& key) const;
};

Value Parse(const std::string& source);
std::string Stringify(const Value& value);

}  // namespace crowdeck::json
