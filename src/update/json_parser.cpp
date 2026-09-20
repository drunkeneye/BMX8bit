#include "update/json_parser.h"

#include <limits.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

static const size_t kMaximumJsonBytes = 1024U * 1024U;

class Parser {
 public:
  Parser(const char *json, size_t length, JsonToken *tokens,
         size_t token_capacity, unsigned max_depth)
      : json_(json),
        length_(length),
        tokens_(tokens),
        capacity_(token_capacity),
        count_(0),
        position_(0),
        max_depth_(max_depth),
        error_(JSON_OK),
        error_offset_(0) {}

  JsonParseResult Run() {
    if (json_ == 0 || tokens_ == 0 || capacity_ == 0 || max_depth_ == 0) {
      return Result(JSON_ERROR_ARGUMENT, 0);
    }
    if (length_ > kMaximumJsonBytes || length_ > UINT32_MAX) {
      return Result(JSON_ERROR_TOO_LARGE, 0);
    }

    SkipWhitespace();
    if (!ParseValue(-1, 0)) {
      return Result(error_, error_offset_);
    }
    SkipWhitespace();
    if (position_ != length_) {
      return Result(JSON_ERROR_TRAILING_DATA, position_);
    }
    return Result(JSON_OK, position_);
  }

 private:
  JsonParseResult Result(JsonError error, size_t offset) const {
    JsonParseResult result;
    result.error = error;
    result.token_count = count_;
    result.error_offset = offset;
    return result;
  }

  void Fail(JsonError error, size_t offset) {
    if (error_ == JSON_OK) {
      error_ = error;
      error_offset_ = offset;
    }
  }

  void SkipWhitespace() {
    while (position_ < length_) {
      const char c = json_[position_];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
        break;
      }
      ++position_;
    }
  }

  int AddToken(JsonTokenType type, size_t start, int parent) {
    if (count_ >= capacity_) {
      Fail(JSON_ERROR_TOKEN_LIMIT, position_);
      return -1;
    }
    JsonToken &token = tokens_[count_];
    token.type = type;
    token.start = static_cast<uint32_t>(start);
    token.end = static_cast<uint32_t>(start);
    token.parent = parent;
    token.child_count = 0;
    if (parent >= 0) {
      ++tokens_[parent].child_count;
    }
    return static_cast<int>(count_++);
  }

  bool ParseValue(int parent, unsigned depth) {
    if (depth >= max_depth_) {
      Fail(JSON_ERROR_TOO_DEEP, position_);
      return false;
    }
    SkipWhitespace();
    if (position_ >= length_) {
      Fail(JSON_ERROR_SYNTAX, position_);
      return false;
    }

    switch (json_[position_]) {
      case '{':
        return ParseObject(parent, depth + 1);
      case '[':
        return ParseArray(parent, depth + 1);
      case '"':
        return ParseString(parent, false) >= 0;
      case 't':
        return ParseLiteral(parent, "true", JSON_TOKEN_TRUE);
      case 'f':
        return ParseLiteral(parent, "false", JSON_TOKEN_FALSE);
      case 'n':
        return ParseLiteral(parent, "null", JSON_TOKEN_NULL);
      default:
        if (json_[position_] == '-' ||
            (json_[position_] >= '0' && json_[position_] <= '9')) {
          return ParseNumber(parent);
        }
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
    }
  }

  bool ParseObject(int parent, unsigned depth) {
    const size_t start = position_++;
    const int object = AddToken(JSON_TOKEN_OBJECT, start, parent);
    if (object < 0) {
      return false;
    }
    SkipWhitespace();
    if (position_ < length_ && json_[position_] == '}') {
      tokens_[object].end = static_cast<uint32_t>(++position_);
      return true;
    }

    while (position_ < length_) {
      if (json_[position_] != '"') {
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
      }
      const int key = ParseString(object, true);
      if (key < 0) {
        return false;
      }
      if (HasDuplicateObjectKey(object, key)) {
        Fail(JSON_ERROR_DUPLICATE_KEY, tokens_[key].start);
        return false;
      }
      SkipWhitespace();
      if (position_ >= length_ || json_[position_] != ':') {
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
      }
      ++position_;
      if (!ParseValue(object, depth)) {
        return false;
      }
      SkipWhitespace();
      if (position_ >= length_) {
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
      }
      if (json_[position_] == '}') {
        tokens_[object].end = static_cast<uint32_t>(++position_);
        return true;
      }
      if (json_[position_] != ',') {
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
      }
      ++position_;
      SkipWhitespace();
    }
    Fail(JSON_ERROR_SYNTAX, position_);
    return false;
  }

  bool HasDuplicateObjectKey(int object, int newest_key) const {
    const JsonToken &newest = tokens_[newest_key];
    const size_t newest_length = newest.end - newest.start;
    for (int i = object + 1; i < newest_key; ++i) {
      const JsonToken &candidate = tokens_[i];
      if (candidate.parent != object || candidate.type != JSON_TOKEN_STRING) {
        continue;
      }
      // Direct children alternate key/value. Keys are the direct strings
      // whose following direct child is a value; child ordinal is cheaper to
      // derive by walking from the object start.
      unsigned ordinal = 0;
      for (int j = object + 1; j <= i; ++j) {
        if (tokens_[j].parent == object) {
          ++ordinal;
        }
      }
      if ((ordinal & 1U) == 0) {
        continue;
      }
      const size_t candidate_length = candidate.end - candidate.start;
      if (candidate_length == newest_length &&
          memcmp(json_ + candidate.start, json_ + newest.start,
                 newest_length) == 0) {
        return true;
      }
    }
    return false;
  }

  bool ParseArray(int parent, unsigned depth) {
    const size_t start = position_++;
    const int array = AddToken(JSON_TOKEN_ARRAY, start, parent);
    if (array < 0) {
      return false;
    }
    SkipWhitespace();
    if (position_ < length_ && json_[position_] == ']') {
      tokens_[array].end = static_cast<uint32_t>(++position_);
      return true;
    }
    while (position_ < length_) {
      if (!ParseValue(array, depth)) {
        return false;
      }
      SkipWhitespace();
      if (position_ >= length_) {
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
      }
      if (json_[position_] == ']') {
        tokens_[array].end = static_cast<uint32_t>(++position_);
        return true;
      }
      if (json_[position_] != ',') {
        Fail(JSON_ERROR_SYNTAX, position_);
        return false;
      }
      ++position_;
      SkipWhitespace();
    }
    Fail(JSON_ERROR_SYNTAX, position_);
    return false;
  }

  int ParseString(int parent, bool object_key) {
    ++position_;  // opening quote
    const size_t start = position_;
    bool escaped = false;
    while (position_ < length_) {
      const unsigned char c = static_cast<unsigned char>(json_[position_]);
      if (c == '"') {
        const size_t end = position_++;
        if (object_key && escaped) {
          Fail(JSON_ERROR_STRING, start);
          return -1;
        }
        const int token = AddToken(JSON_TOKEN_STRING, start, parent);
        if (token >= 0) {
          tokens_[token].end = static_cast<uint32_t>(end);
        }
        return token;
      }
      if (c < 0x20) {
        Fail(JSON_ERROR_STRING, position_);
        return -1;
      }
      if (c == '\\') {
        escaped = true;
        if (!ParseEscape()) {
          return -1;
        }
        continue;
      }
      if (object_key && (c < 0x21 || c > 0x7e)) {
        Fail(JSON_ERROR_STRING, position_);
        return -1;
      }
      if (c >= 0x80 && !ConsumeUtf8()) {
        return -1;
      }
      ++position_;
    }
    Fail(JSON_ERROR_STRING, position_);
    return -1;
  }

  bool ParseEscape() {
    const size_t slash = position_++;
    if (position_ >= length_) {
      Fail(JSON_ERROR_STRING, slash);
      return false;
    }
    const char escaped = json_[position_++];
    if (strchr("\"\\/bfnrt", escaped) != 0) {
      return true;
    }
    if (escaped != 'u' || position_ + 4 > length_) {
      Fail(JSON_ERROR_STRING, slash);
      return false;
    }
    uint32_t codepoint = 0;
    if (!ParseHex4(position_, &codepoint)) {
      Fail(JSON_ERROR_STRING, position_);
      return false;
    }
    position_ += 4;
    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
      if (position_ + 6 > length_ || json_[position_] != '\\' ||
          json_[position_ + 1] != 'u') {
        Fail(JSON_ERROR_STRING, position_);
        return false;
      }
      uint32_t low = 0;
      if (!ParseHex4(position_ + 2, &low) || low < 0xdc00 || low > 0xdfff) {
        Fail(JSON_ERROR_STRING, position_ + 2);
        return false;
      }
      position_ += 6;
    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
      Fail(JSON_ERROR_STRING, position_ - 4);
      return false;
    }
    return true;
  }

  bool ParseHex4(size_t offset, uint32_t *value) const {
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
      const char c = json_[offset + i];
      uint32_t digit;
      if (c >= '0' && c <= '9') {
        digit = static_cast<uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
      result = (result << 4) | digit;
    }
    *value = result;
    return true;
  }

  bool ConsumeUtf8() {
    const size_t start = position_;
    const unsigned char first = static_cast<unsigned char>(json_[position_]);
    unsigned count;
    uint32_t codepoint;
    if (first >= 0xc2 && first <= 0xdf) {
      count = 2;
      codepoint = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
      count = 3;
      codepoint = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
      count = 4;
      codepoint = first & 0x07;
    } else {
      Fail(JSON_ERROR_STRING, start);
      return false;
    }
    if (position_ + count > length_) {
      Fail(JSON_ERROR_STRING, start);
      return false;
    }
    for (unsigned i = 1; i < count; ++i) {
      const unsigned char c = static_cast<unsigned char>(json_[position_ + i]);
      if ((c & 0xc0) != 0x80) {
        Fail(JSON_ERROR_STRING, position_ + i);
        return false;
      }
      codepoint = (codepoint << 6) | (c & 0x3f);
    }
    if ((count == 3 && codepoint < 0x800) ||
        (count == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        codepoint > 0x10ffff) {
      Fail(JSON_ERROR_STRING, start);
      return false;
    }
    position_ += count - 1;
    return true;
  }

  bool ParseLiteral(int parent, const char *literal, JsonTokenType type) {
    const size_t literal_length = strlen(literal);
    const size_t start = position_;
    if (position_ + literal_length > length_ ||
        memcmp(json_ + position_, literal, literal_length) != 0) {
      Fail(JSON_ERROR_SYNTAX, position_);
      return false;
    }
    position_ += literal_length;
    const int token = AddToken(type, start, parent);
    if (token < 0) {
      return false;
    }
    tokens_[token].end = static_cast<uint32_t>(position_);
    return true;
  }

  bool ParseNumber(int parent) {
    const size_t start = position_;
    if (json_[position_] == '-') {
      ++position_;
      if (position_ >= length_) {
        Fail(JSON_ERROR_NUMBER, start);
        return false;
      }
    }
    if (json_[position_] == '0') {
      ++position_;
      if (position_ < length_ && json_[position_] >= '0' &&
          json_[position_] <= '9') {
        Fail(JSON_ERROR_NUMBER, position_);
        return false;
      }
    } else if (json_[position_] >= '1' && json_[position_] <= '9') {
      do {
        ++position_;
      } while (position_ < length_ && json_[position_] >= '0' &&
               json_[position_] <= '9');
    } else {
      Fail(JSON_ERROR_NUMBER, position_);
      return false;
    }

    if (position_ < length_ && json_[position_] == '.') {
      ++position_;
      if (position_ >= length_ || json_[position_] < '0' ||
          json_[position_] > '9') {
        Fail(JSON_ERROR_NUMBER, position_);
        return false;
      }
      while (position_ < length_ && json_[position_] >= '0' &&
             json_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < length_ &&
        (json_[position_] == 'e' || json_[position_] == 'E')) {
      ++position_;
      if (position_ < length_ &&
          (json_[position_] == '+' || json_[position_] == '-')) {
        ++position_;
      }
      if (position_ >= length_ || json_[position_] < '0' ||
          json_[position_] > '9') {
        Fail(JSON_ERROR_NUMBER, position_);
        return false;
      }
      while (position_ < length_ && json_[position_] >= '0' &&
             json_[position_] <= '9') {
        ++position_;
      }
    }

    const int token = AddToken(JSON_TOKEN_NUMBER, start, parent);
    if (token < 0) {
      return false;
    }
    tokens_[token].end = static_cast<uint32_t>(position_);
    return true;
  }

  const char *json_;
  size_t length_;
  JsonToken *tokens_;
  size_t capacity_;
  size_t count_;
  size_t position_;
  unsigned max_depth_;
  JsonError error_;
  size_t error_offset_;
};

static bool AppendUtf8(uint32_t codepoint, char *output, size_t output_size,
                       size_t *position) {
  unsigned bytes;
  if (codepoint <= 0x7f) {
    bytes = 1;
  } else if (codepoint <= 0x7ff) {
    bytes = 2;
  } else if (codepoint <= 0xffff) {
    bytes = 3;
  } else {
    bytes = 4;
  }
  if (*position + bytes >= output_size) {
    return false;
  }
  if (bytes == 1) {
    output[(*position)++] = static_cast<char>(codepoint);
  } else if (bytes == 2) {
    output[(*position)++] = static_cast<char>(0xc0 | (codepoint >> 6));
    output[(*position)++] = static_cast<char>(0x80 | (codepoint & 0x3f));
  } else if (bytes == 3) {
    output[(*position)++] = static_cast<char>(0xe0 | (codepoint >> 12));
    output[(*position)++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
    output[(*position)++] = static_cast<char>(0x80 | (codepoint & 0x3f));
  } else {
    output[(*position)++] = static_cast<char>(0xf0 | (codepoint >> 18));
    output[(*position)++] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
    output[(*position)++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
    output[(*position)++] = static_cast<char>(0x80 | (codepoint & 0x3f));
  }
  return true;
}

static int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static bool DecodeHex4(const char *input, uint32_t *value) {
  uint32_t result = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const int digit = HexDigit(input[i]);
    if (digit < 0) return false;
    result = (result << 4) | static_cast<uint32_t>(digit);
  }
  *value = result;
  return true;
}

}  // namespace

JsonParseResult ParseJson(const char *json, size_t length, JsonToken *tokens,
                          size_t token_capacity, unsigned max_depth) {
  Parser parser(json, length, tokens, token_capacity, max_depth);
  return parser.Run();
}

int JsonFindObjectMember(const char *json, const JsonToken *tokens,
                         size_t token_count, int object_index,
                         const char *name) {
  if (json == 0 || tokens == 0 || name == 0 || object_index < 0 ||
      static_cast<size_t>(object_index) >= token_count ||
      tokens[object_index].type != JSON_TOKEN_OBJECT) {
    return -1;
  }
  bool expect_key = true;
  bool key_matches = false;
  for (size_t i = static_cast<size_t>(object_index) + 1; i < token_count; ++i) {
    if (tokens[i].start >= tokens[object_index].end) {
      break;
    }
    if (tokens[i].parent != object_index) {
      continue;
    }
    if (expect_key) {
      if (tokens[i].type != JSON_TOKEN_STRING) {
        return -1;
      }
      key_matches = JsonStringEquals(json, tokens[i], name);
    } else if (key_matches) {
      return static_cast<int>(i);
    }
    expect_key = !expect_key;
  }
  return -1;
}

bool JsonStringEquals(const char *json, const JsonToken &token,
                      const char *expected) {
  if (json == 0 || expected == 0 || token.type != JSON_TOKEN_STRING) {
    return false;
  }
  const size_t expected_length = strlen(expected);
  const size_t token_length = token.end - token.start;
  return token_length == expected_length &&
         memcmp(json + token.start, expected, token_length) == 0;
}

JsonError JsonCopyString(const char *json, const JsonToken &token,
                         char *output, size_t output_size) {
  if (json == 0 || output == 0 || output_size == 0) {
    return JSON_ERROR_ARGUMENT;
  }
  if (token.type != JSON_TOKEN_STRING) {
    return JSON_ERROR_TYPE;
  }
  size_t out = 0;
  size_t in = token.start;
  while (in < token.end) {
    unsigned char c = static_cast<unsigned char>(json[in++]);
    if (c != '\\') {
      if (out + 1 >= output_size) return JSON_ERROR_OUTPUT_TOO_SMALL;
      output[out++] = static_cast<char>(c);
      continue;
    }
    if (in >= token.end) return JSON_ERROR_STRING;
    c = static_cast<unsigned char>(json[in++]);
    char decoded = 0;
    switch (c) {
      case '"': decoded = '"'; break;
      case '\\': decoded = '\\'; break;
      case '/': decoded = '/'; break;
      case 'b': decoded = '\b'; break;
      case 'f': decoded = '\f'; break;
      case 'n': decoded = '\n'; break;
      case 'r': decoded = '\r'; break;
      case 't': decoded = '\t'; break;
      case 'u': {
        if (in + 4 > token.end) return JSON_ERROR_STRING;
        uint32_t codepoint = 0;
        if (!DecodeHex4(json + in, &codepoint)) return JSON_ERROR_STRING;
        in += 4;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
          if (in + 6 > token.end || json[in] != '\\' || json[in + 1] != 'u') {
            return JSON_ERROR_STRING;
          }
          uint32_t low = 0;
          if (!DecodeHex4(json + in + 2, &low) || low < 0xdc00 ||
              low > 0xdfff) {
            return JSON_ERROR_STRING;
          }
          in += 6;
          codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                      (low - 0xdc00);
        }
        if (!AppendUtf8(codepoint, output, output_size, &out)) {
          return JSON_ERROR_OUTPUT_TOO_SMALL;
        }
        continue;
      }
      default:
        return JSON_ERROR_STRING;
    }
    if (out + 1 >= output_size) return JSON_ERROR_OUTPUT_TOO_SMALL;
    output[out++] = decoded;
  }
  output[out] = '\0';
  return JSON_OK;
}

JsonError JsonGetUint64(const char *json, const JsonToken &token,
                        uint64_t *value) {
  if (json == 0 || value == 0) return JSON_ERROR_ARGUMENT;
  if (token.type != JSON_TOKEN_NUMBER || token.start >= token.end) {
    return JSON_ERROR_TYPE;
  }
  uint64_t result = 0;
  for (uint32_t i = token.start; i < token.end; ++i) {
    const char c = json[i];
    if (c < '0' || c > '9') return JSON_ERROR_TYPE;
    const uint64_t digit = static_cast<uint64_t>(c - '0');
    if (result > (UINT64_MAX - digit) / 10U) return JSON_ERROR_RANGE;
    result = result * 10U + digit;
  }
  *value = result;
  return JSON_OK;
}

JsonError JsonGetBool(const JsonToken &token, bool *value) {
  if (value == 0) return JSON_ERROR_ARGUMENT;
  if (token.type == JSON_TOKEN_TRUE) {
    *value = true;
    return JSON_OK;
  }
  if (token.type == JSON_TOKEN_FALSE) {
    *value = false;
    return JSON_OK;
  }
  return JSON_ERROR_TYPE;
}

const char *JsonErrorString(JsonError error) {
  switch (error) {
    case JSON_OK: return "ok";
    case JSON_ERROR_ARGUMENT: return "invalid argument";
    case JSON_ERROR_TOO_LARGE: return "JSON too large";
    case JSON_ERROR_TOO_DEEP: return "JSON nesting too deep";
    case JSON_ERROR_TOKEN_LIMIT: return "JSON token limit exceeded";
    case JSON_ERROR_SYNTAX: return "invalid JSON syntax";
    case JSON_ERROR_STRING: return "invalid JSON string";
    case JSON_ERROR_NUMBER: return "invalid JSON number";
    case JSON_ERROR_DUPLICATE_KEY: return "duplicate JSON object key";
    case JSON_ERROR_TRAILING_DATA: return "trailing JSON data";
    case JSON_ERROR_NOT_FOUND: return "JSON member not found";
    case JSON_ERROR_TYPE: return "unexpected JSON type";
    case JSON_ERROR_RANGE: return "JSON number out of range";
    case JSON_ERROR_OUTPUT_TOO_SMALL: return "output buffer too small";
  }
  return "unknown JSON error";
}

}  // namespace update
}  // namespace bmx
