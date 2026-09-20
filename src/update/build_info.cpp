#include "update/build_info.h"

#include <string.h>

namespace bmx {
namespace update {

uint32_t CompiledUpdaterAbi()
{
    return static_cast<uint32_t>(BMX_UPDATE_UPDATER_ABI);
}

namespace {

ConfigArea AreaFromToken(const char *json, const JsonToken &token) {
    if (JsonStringEquals(json, token, "machines")) return ConfigArea::Machines;
    if (JsonStringEquals(json, token, "config_managed_block")) {
        return ConfigArea::ConfigManagedBlock;
    }
    if (JsonStringEquals(json, token, "cmdline_managed")) {
        return ConfigArea::CmdlineManagedKeys;
    }
    if (JsonStringEquals(json, token, "network")) return ConfigArea::Network;
    if (JsonStringEquals(json, token, "settings")) return ConfigArea::Settings;
    if (JsonStringEquals(json, token, "update_state")) {
        return ConfigArea::UpdateState;
    }
    return ConfigArea::Unknown;
}

bool ExactRoot(const char *json, const JsonToken *tokens, size_t token_count) {
    static const char *const kFields[] = {
        "build_info_version", "version", "release_sequence", "release_epoch",
        "board_family", "source_commit", "updater_abi",
        "configuration_schemas"
    };
    if (token_count == 0U || tokens[0].type != JSON_TOKEN_OBJECT ||
        tokens[0].child_count != sizeof(kFields) / sizeof(kFields[0]) * 2U) {
        return false;
    }
    for (size_t i = 0U; i < sizeof(kFields) / sizeof(kFields[0]); ++i) {
        if (JsonFindObjectMember(json, tokens, token_count, 0, kFields[i]) < 0) {
            return false;
        }
    }
    size_t direct_ordinal = 0U;
    for (size_t i = 1U; i < token_count; ++i) {
        if (tokens[i].start >= tokens[0].end) break;
        if (tokens[i].parent != 0) continue;
        if ((direct_ordinal++ & 1U) != 0U) continue;
        bool known = false;
        for (size_t field = 0U; field < sizeof(kFields) / sizeof(kFields[0]);
             ++field) {
            if (JsonStringEquals(json, tokens[i], kFields[field])) {
                known = true;
                break;
            }
        }
        if (!known) return false;
    }
    return true;
}

bool GetUint(const char *json, const JsonToken *tokens, int token,
             uint64_t minimum, uint64_t maximum, uint64_t *value) {
    uint64_t result = 0U;
    if (token < 0 || JsonGetUint64(json, tokens[token], &result) != JSON_OK ||
        result < minimum || result > maximum) return false;
    *value = result;
    return true;
}

bool IsVersion(const char *value) {
    const size_t size = strlen(value);
    if (size == 0U || size > 64U) return false;
    for (size_t i = 0U; i < size; ++i) {
        const char c = value[i];
        const bool alpha_numeric = (c >= '0' && c <= '9') ||
                                   (c >= 'A' && c <= 'Z') ||
                                   (c >= 'a' && c <= 'z');
        if (!alpha_numeric && (i == 0U || (c != '.' && c != '_' && c != '+' &&
                                           c != '-'))) return false;
    }
    return true;
}

bool IsCommit(const char *value) {
    const size_t size = strlen(value);
    if (size != 40U && size != 64U) return false;
    for (size_t i = 0U; i < size; ++i) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f'))) return false;
    }
    return true;
}

}  // namespace

BuildInfoStatus ParseBuildInfo(ByteView encoded, JsonToken *tokens,
                              size_t token_capacity,
                              InstalledBuildInfo *build_info,
                              JsonParseResult *json_result) {
    if (encoded.data == 0 || tokens == 0 || token_capacity == 0U ||
        build_info == 0 || json_result == 0) {
        return BuildInfoStatus::InvalidArgument;
    }
    if (encoded.size < 3U || encoded.size > kMaximumBuildInfoBytes ||
        encoded.data[0] != '{' || encoded.data[encoded.size - 1U] != '\n' ||
        encoded.data[encoded.size - 2U] != '}') {
        return BuildInfoStatus::InvalidSize;
    }
    const char *json = reinterpret_cast<const char *>(encoded.data);
    *json_result = ParseJson(json, encoded.size, tokens, token_capacity, 16U);
    if (json_result->error != JSON_OK) {
        return json_result->error == JSON_ERROR_TOKEN_LIMIT
                   ? BuildInfoStatus::StorageTooSmall
                   : BuildInfoStatus::JsonInvalid;
    }
    if (!ExactRoot(json, tokens, json_result->token_count)) {
        return BuildInfoStatus::FieldsInvalid;
    }
    memset(build_info, 0, sizeof(*build_info));
    const int format_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "build_info_version");
    uint64_t format = 0U;
    if (!GetUint(json, tokens, format_token, 1U, 1U, &format)) {
        return BuildInfoStatus::UnsupportedVersion;
    }
    const int version_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "version");
    const int sequence_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "release_sequence");
    const int epoch_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "release_epoch");
    const int board_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "board_family");
    const int commit_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "source_commit");
    const int abi_token = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "updater_abi");
    char board[8];
    uint64_t abi = 0U;
    if (JsonCopyString(json, tokens[version_token], build_info->version,
                       sizeof(build_info->version)) != JSON_OK ||
        !IsVersion(build_info->version) ||
        !GetUint(json, tokens, sequence_token, 1U, INT32_MAX,
                 &build_info->release_sequence) ||
        !GetUint(json, tokens, epoch_token, 1U, INT64_MAX,
                 &build_info->release_epoch) ||
        JsonCopyString(json, tokens[board_token], board, sizeof(board)) != JSON_OK ||
        JsonCopyString(json, tokens[commit_token], build_info->source_commit,
                       sizeof(build_info->source_commit)) != JSON_OK ||
        !IsCommit(build_info->source_commit) ||
        !GetUint(json, tokens, abi_token, 1U, INT32_MAX, &abi)) {
        return BuildInfoStatus::FieldsInvalid;
    }
    build_info->updater_abi = static_cast<uint32_t>(abi);
    if (strcmp(board, "pi4") == 0) {
        build_info->board = BoardFamily::Pi4Pi400;
    } else if (strcmp(board, "pi5") == 0) {
        build_info->board = BoardFamily::Pi5Pi500;
    } else {
        return BuildInfoStatus::FieldsInvalid;
    }

    const int schemas = JsonFindObjectMember(
        json, tokens, json_result->token_count, 0, "configuration_schemas");
    if (schemas < 0 || tokens[schemas].type != JSON_TOKEN_OBJECT ||
        tokens[schemas].child_count != 12U) {
        return BuildInfoStatus::UnsupportedSchema;
    }
    bool expect_key = true;
    ConfigArea current_area = ConfigArea::Unknown;
    bool seen[7] = {false, false, false, false, false, false, false};
    size_t count = 0U;
    for (size_t i = static_cast<size_t>(schemas) + 1U;
         i < json_result->token_count; ++i) {
        if (tokens[i].start >= tokens[schemas].end) break;
        if (tokens[i].parent != schemas) continue;
        if (expect_key) {
            current_area = AreaFromToken(json, tokens[i]);
            const size_t area_index = static_cast<size_t>(current_area);
            if (!IsKnownConfigArea(current_area) || area_index >= 7U ||
                seen[area_index]) return BuildInfoStatus::UnsupportedSchema;
            seen[area_index] = true;
        } else {
            uint64_t schema_version = 0U;
            if (count >= kMaximumConfigAreas ||
                JsonGetUint64(json, tokens[i], &schema_version) != JSON_OK ||
                schema_version > INT32_MAX) {
                return BuildInfoStatus::UnsupportedSchema;
            }
            build_info->schemas[count].area = current_area;
            build_info->schemas[count].version =
                static_cast<uint32_t>(schema_version);
            ++count;
        }
        expect_key = !expect_key;
    }
    if (!expect_key || count != 6U) return BuildInfoStatus::UnsupportedSchema;
    build_info->schema_count = count;
    return BuildInfoStatus::Ok;
}

const char *BuildInfoStatusString(BuildInfoStatus status) {
    switch (status) {
        case BuildInfoStatus::Ok: return "ok";
        case BuildInfoStatus::InvalidArgument: return "invalid argument";
        case BuildInfoStatus::InvalidSize: return "BMX-BUILD size/format invalid";
        case BuildInfoStatus::JsonInvalid: return "BMX-BUILD JSON invalid";
        case BuildInfoStatus::UnsupportedVersion: return "unsupported BMX-BUILD version";
        case BuildInfoStatus::FieldsInvalid: return "BMX-BUILD fields invalid";
        case BuildInfoStatus::UnsupportedSchema: return "BMX-BUILD schemas unsupported";
        case BuildInfoStatus::StorageTooSmall: return "BMX-BUILD parse storage too small";
    }
    return "unknown BMX-BUILD error";
}

}  // namespace update
}  // namespace bmx
