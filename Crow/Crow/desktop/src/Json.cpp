#include "crowdeck/Json.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace crowdeck::json {

const Value::Object* Value::AsObject() const {
  return std::get_if<Object>(&data);
}

const Value::Array* Value::AsArray() const {
  return std::get_if<Array>(&data);
}

const std::string* Value::AsString() const {
  return std::get_if<std::string>(&data);
}

const Value* Value::Find(const std::string& key) const {
  const auto* object = AsObject();
  if (!object) return nullptr;
  const auto found = object->find(key);
  return found == object->end() ? nullptr : &found->second;
}

namespace {

void AppendUtf8(std::string& output, uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

class Parser {
 public:
  explicit Parser(const std::string& source) : source_(source) {}

  Value ParseDocument() {
    SkipWhitespace();
    Value result = ParseValue();
    SkipWhitespace();
    if (position_ != source_.size()) Fail("Unexpected trailing data.");
    return result;
  }

 private:
  Value ParseValue() {
    SkipWhitespace();
    if (position_ >= source_.size()) Fail("Unexpected end of input.");
    switch (source_[position_]) {
      case '{':
        return ParseObject();
      case '[':
        return ParseArray();
      case '"':
        return Value(ParseString());
      case 't':
        Expect("true");
        return Value(true);
      case 'f':
        Expect("false");
        return Value(false);
      case 'n':
        Expect("null");
        return Value();
      default:
        if (source_[position_] == '-' ||
            (source_[position_] >= '0' && source_[position_] <= '9')) {
          return ParseNumber();
        }
        Fail("Invalid value.");
    }
    return Value();
  }

  Value ParseObject() {
    ++position_;
    Value::Object object;
    SkipWhitespace();
    if (Consume('}')) return Value(std::move(object));
    while (true) {
      SkipWhitespace();
      if (position_ >= source_.size() || source_[position_] != '"') {
        Fail("Expected an object key.");
      }
      std::string key = ParseString();
      SkipWhitespace();
      if (!Consume(':')) Fail("Expected ':' after object key.");
      Value value = ParseValue();
      if (!object.emplace(std::move(key), std::move(value)).second) {
        Fail("Duplicate object key.");
      }
      SkipWhitespace();
      if (Consume('}')) break;
      if (!Consume(',')) Fail("Expected ',' or '}' in object.");
    }
    return Value(std::move(object));
  }

  Value ParseArray() {
    ++position_;
    Value::Array array;
    SkipWhitespace();
    if (Consume(']')) return Value(std::move(array));
    while (true) {
      array.push_back(ParseValue());
      SkipWhitespace();
      if (Consume(']')) break;
      if (!Consume(',')) Fail("Expected ',' or ']' in array.");
    }
    return Value(std::move(array));
  }

  std::string ParseString() {
    if (!Consume('"')) Fail("Expected a string.");
    std::string output;
    while (position_ < source_.size()) {
      const unsigned char value = source_[position_++];
      if (value == '"') return output;
      if (value < 0x20) Fail("Control character in string.");
      if (value != '\\') {
        output.push_back(static_cast<char>(value));
        continue;
      }
      if (position_ >= source_.size()) Fail("Incomplete escape sequence.");
      const char escaped = source_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          uint32_t codepoint = ParseHex4();
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (position_ + 2 > source_.size() ||
                source_[position_] != '\\' || source_[position_ + 1] != 'u') {
              Fail("Missing low surrogate.");
            }
            position_ += 2;
            const uint32_t low = ParseHex4();
            if (low < 0xDC00 || low > 0xDFFF) Fail("Invalid low surrogate.");
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            Fail("Unexpected low surrogate.");
          }
          AppendUtf8(output, codepoint);
          break;
        }
        default:
          Fail("Invalid escape sequence.");
      }
    }
    Fail("Unterminated string.");
    return {};
  }

  uint32_t ParseHex4() {
    if (position_ + 4 > source_.size()) Fail("Incomplete Unicode escape.");
    uint32_t value = 0;
    for (int count = 0; count < 4; ++count) {
      const char digit = source_[position_++];
      value <<= 4;
      if (digit >= '0' && digit <= '9') value |= digit - '0';
      else if (digit >= 'a' && digit <= 'f') value |= digit - 'a' + 10;
      else if (digit >= 'A' && digit <= 'F') value |= digit - 'A' + 10;
      else Fail("Invalid Unicode escape.");
    }
    return value;
  }

  Value ParseNumber() {
    const size_t start = position_;
    if (Consume('-') && position_ >= source_.size()) Fail("Invalid number.");
    if (Consume('0')) {
      if (position_ < source_.size() && source_[position_] >= '0' &&
          source_[position_] <= '9') {
        Fail("Leading zero in number.");
      }
    } else {
      ParseDigits();
    }
    if (Consume('.')) ParseDigits();
    if (Consume('e') || Consume('E')) {
      if (!Consume('+')) Consume('-');
      ParseDigits();
    }
    try {
      return Value(std::stod(source_.substr(start, position_ - start)));
    } catch (...) {
      Fail("Invalid number.");
    }
    return Value();
  }

  void ParseDigits() {
    const size_t start = position_;
    while (position_ < source_.size() && source_[position_] >= '0' &&
           source_[position_] <= '9') {
      ++position_;
    }
    if (position_ == start) Fail("Expected a digit.");
  }

  void Expect(const char* value) {
    while (*value) {
      if (position_ >= source_.size() || source_[position_++] != *value++) {
        Fail("Invalid literal.");
      }
    }
  }

  void SkipWhitespace() {
    while (position_ < source_.size() &&
           (source_[position_] == ' ' || source_[position_] == '\n' ||
            source_[position_] == '\r' || source_[position_] == '\t')) {
      ++position_;
    }
  }

  bool Consume(char value) {
    if (position_ < source_.size() && source_[position_] == value) {
      ++position_;
      return true;
    }
    return false;
  }

  [[noreturn]] void Fail(const char* message) const {
    throw std::runtime_error(std::string(message) + " At byte " +
                             std::to_string(position_) + ".");
  }

  const std::string& source_;
  size_t position_ = 0;
};

void WriteString(std::ostringstream& output, const std::string& value) {
  output << '"';
  for (unsigned char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20) {
          output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(character) << std::dec;
        } else {
          output << static_cast<char>(character);
        }
    }
  }
  output << '"';
}

void WriteValue(std::ostringstream& output, const Value& value) {
  if (std::holds_alternative<std::nullptr_t>(value.data)) {
    output << "null";
  } else if (const auto* boolean = std::get_if<bool>(&value.data)) {
    output << (*boolean ? "true" : "false");
  } else if (const auto* number = std::get_if<double>(&value.data)) {
    output << std::setprecision(15) << *number;
  } else if (const auto* string = std::get_if<std::string>(&value.data)) {
    WriteString(output, *string);
  } else if (const auto* array = std::get_if<Value::Array>(&value.data)) {
    output << '[';
    for (size_t index = 0; index < array->size(); ++index) {
      if (index) output << ',';
      WriteValue(output, (*array)[index]);
    }
    output << ']';
  } else {
    const auto& object = std::get<Value::Object>(value.data);
    output << '{';
    bool first = true;
    for (const auto& [key, child] : object) {
      if (!first) output << ',';
      first = false;
      WriteString(output, key);
      output << ':';
      WriteValue(output, child);
    }
    output << '}';
  }
}

}  // namespace

Value Parse(const std::string& source) {
  return Parser(source).ParseDocument();
}

std::string Stringify(const Value& value) {
  std::ostringstream output;
  WriteValue(output, value);
  return output.str();
}

}  // namespace crowdeck::json
