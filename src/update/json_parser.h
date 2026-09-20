#ifndef BMX_UPDATE_JSON_PARSER_H
#define BMX_UPDATE_JSON_PARSER_H

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace update {

enum JsonTokenType {
  JSON_TOKEN_INVALID = 0,
  JSON_TOKEN_OBJECT,
  JSON_TOKEN_ARRAY,
  JSON_TOKEN_STRING,
  JSON_TOKEN_NUMBER,
  JSON_TOKEN_TRUE,
  JSON_TOKEN_FALSE,
  JSON_TOKEN_NULL
};

struct JsonToken {
  JsonTokenType type;
  uint32_t start;
  uint32_t end;
  int32_t parent;
  uint32_t child_count;
};

enum JsonError {
  JSON_OK = 0,
  JSON_ERROR_ARGUMENT,
  JSON_ERROR_TOO_LARGE,
  JSON_ERROR_TOO_DEEP,
  JSON_ERROR_TOKEN_LIMIT,
  JSON_ERROR_SYNTAX,
  JSON_ERROR_STRING,
  JSON_ERROR_NUMBER,
  JSON_ERROR_DUPLICATE_KEY,
  JSON_ERROR_TRAILING_DATA,
  JSON_ERROR_NOT_FOUND,
  JSON_ERROR_TYPE,
  JSON_ERROR_RANGE,
  JSON_ERROR_OUTPUT_TOO_SMALL
};

struct JsonParseResult {
  JsonError error;
  size_t token_count;
  size_t error_offset;
};

// Strict, allocation-free RFC 8259 parser. Object keys must be printable
// unescaped ASCII. This deliberately restricted profile makes duplicate-key
// rejection unambiguous and covers both BMX manifests and GitHub's API keys.
JsonParseResult ParseJson(const char *json, size_t length,
                          JsonToken *tokens, size_t token_capacity,
                          unsigned max_depth = 32);

int JsonFindObjectMember(const char *json, const JsonToken *tokens,
                         size_t token_count, int object_index,
                         const char *name);

bool JsonStringEquals(const char *json, const JsonToken &token,
                      const char *expected);

JsonError JsonCopyString(const char *json, const JsonToken &token,
                         char *output, size_t output_size);

JsonError JsonGetUint64(const char *json, const JsonToken &token,
                        uint64_t *value);

JsonError JsonGetBool(const JsonToken &token, bool *value);

const char *JsonErrorString(JsonError error);

}  // namespace update
}  // namespace bmx

#endif
