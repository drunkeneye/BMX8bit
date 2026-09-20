#include "update/draft_test_ticket.h"

#include "update/github_release_parser.h"

#include <limits.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

bool ExactRoot(const char *json, const JsonToken *tokens, size_t token_count) {
    static const char *const kFields[] = {
        "ticket_version", "repository", "prepare_id", "tag", "draft_id",
        "release_sequence", "manifest_sha256", "github_app_client_id",
        "signing_key_id", "issued_epoch", "expires_epoch"
    };
    if (token_count == 0U || tokens[0].type != JSON_TOKEN_OBJECT ||
        tokens[0].child_count != sizeof(kFields) / sizeof(kFields[0]) * 2U) {
        return false;
    }
    for (size_t index = 0U;
         index < sizeof(kFields) / sizeof(kFields[0]); ++index) {
        if (JsonFindObjectMember(json, tokens, token_count, 0,
                                 kFields[index]) < 0) return false;
    }
    size_t ordinal = 0U;
    for (size_t index = 1U; index < token_count; ++index) {
        if (tokens[index].start >= tokens[0].end) break;
        if (tokens[index].parent != 0) continue;
        if ((ordinal++ & 1U) != 0U) continue;
        bool known = false;
        for (size_t field = 0U;
             field < sizeof(kFields) / sizeof(kFields[0]); ++field) {
            if (JsonStringEquals(json, tokens[index], kFields[field])) {
                known = true;
                break;
            }
        }
        if (!known) return false;
    }
    return true;
}

bool SafeIdentifier(const char *value, size_t maximum, bool allow_plus) {
    if (value == 0) return false;
    const size_t size = strlen(value);
    if (size == 0U || size > maximum) return false;
    for (size_t index = 0U; index < size; ++index) {
        const char c = value[index];
        const bool alpha_numeric = (c >= '0' && c <= '9') ||
                                   (c >= 'A' && c <= 'Z') ||
                                   (c >= 'a' && c <= 'z');
        if (!alpha_numeric && (index == 0U ||
            (c != '.' && c != '_' && c != '-' && (!allow_plus || c != '+')))) {
            return false;
        }
    }
    return true;
}

bool DecodeHexDigest(const char *value,
                     uint8_t output[kSha256DigestBytes]) {
    if (value == 0 || strlen(value) != kSha256DigestBytes * 2U) return false;
    for (size_t index = 0U; index < kSha256DigestBytes; ++index) {
        uint8_t byte = 0U;
        for (unsigned half = 0U; half < 2U; ++half) {
            const char c = value[index * 2U + half];
            uint8_t nibble = 0U;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<uint8_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<uint8_t>(c - 'a' + 10);
            } else {
                return false;
            }
            byte = static_cast<uint8_t>((byte << 4U) | nibble);
        }
        output[index] = byte;
    }
    return true;
}

bool ReadUint(const char *json, const JsonToken *tokens, size_t token_count,
              const char *name, uint64_t minimum, uint64_t maximum,
              uint64_t *value) {
    const int member = JsonFindObjectMember(json, tokens, token_count, 0, name);
    uint64_t parsed = 0U;
    if (member < 0 || JsonGetUint64(json, tokens[member], &parsed) != JSON_OK ||
        parsed < minimum || parsed > maximum) return false;
    *value = parsed;
    return true;
}

bool ReadString(const char *json, const JsonToken *tokens, size_t token_count,
                const char *name, char *output, size_t output_size) {
    const int member = JsonFindObjectMember(json, tokens, token_count, 0, name);
    return member >= 0 && JsonCopyString(json, tokens[member], output,
                                         output_size) == JSON_OK;
}

}  // namespace

DraftTestTicketStatus VerifyDraftTestTicket(
    ByteView ticket_bytes, ByteView signature_envelope, JsonToken *tokens,
    size_t token_capacity, const TrustedReleaseKey *trusted_keys,
    size_t trusted_key_count, VerifyP256SignatureFunction verify_function,
    void *verify_context, uint64_t current_epoch, DraftTestTicket *ticket,
    JsonParseResult *json_result) {
    if (json_result != 0) {
        json_result->error = JSON_ERROR_ARGUMENT;
        json_result->token_count = 0U;
        json_result->error_offset = 0U;
    }
    if (ticket_bytes.data == 0 || signature_envelope.data == 0 ||
        tokens == 0 || token_capacity == 0U || trusted_keys == 0 ||
        trusted_key_count == 0U || verify_function == 0 || ticket == 0 ||
        json_result == 0) return DraftTestTicketStatus::InvalidArgument;
    memset(ticket, 0, sizeof(*ticket));
    if (ticket_bytes.size < 3U ||
        ticket_bytes.size > kMaximumDraftTestTicketBytes ||
        ticket_bytes.data[0] != '{' ||
        ticket_bytes.data[ticket_bytes.size - 2U] != '}' ||
        ticket_bytes.data[ticket_bytes.size - 1U] != '\n') {
        return DraftTestTicketStatus::InvalidSize;
    }

    const char *json = reinterpret_cast<const char *>(ticket_bytes.data);
    *json_result = ParseJson(json, ticket_bytes.size, tokens, token_capacity,
                             8U);
    if (json_result->error != JSON_OK) {
        return json_result->error == JSON_ERROR_TOKEN_LIMIT
                   ? DraftTestTicketStatus::StorageTooSmall
                   : DraftTestTicketStatus::JsonInvalid;
    }
    if (!ExactRoot(json, tokens, json_result->token_count)) {
        return DraftTestTicketStatus::FieldsInvalid;
    }

    uint64_t version = 0U;
    char digest[kSha256DigestBytes * 2U + 1U];
    if (!ReadUint(json, tokens, json_result->token_count, "ticket_version",
                  1U, 1U, &version) ||
        !ReadString(json, tokens, json_result->token_count, "repository",
                    ticket->repository, sizeof(ticket->repository)) ||
        !ReadString(json, tokens, json_result->token_count, "prepare_id",
                    ticket->prepare_id, sizeof(ticket->prepare_id)) ||
        !ReadString(json, tokens, json_result->token_count, "tag",
                    ticket->tag, sizeof(ticket->tag)) ||
        !ReadString(json, tokens, json_result->token_count,
                    "github_app_client_id", ticket->github_app_client_id,
                    sizeof(ticket->github_app_client_id)) ||
        !ReadString(json, tokens, json_result->token_count, "signing_key_id",
                    ticket->signing_key_id, sizeof(ticket->signing_key_id)) ||
        !ReadString(json, tokens, json_result->token_count, "manifest_sha256",
                    digest, sizeof(digest)) ||
        !ReadUint(json, tokens, json_result->token_count, "draft_id", 1U,
                  INT64_MAX, &ticket->draft_id) ||
        !ReadUint(json, tokens, json_result->token_count, "release_sequence",
                  1U, INT64_MAX, &ticket->release_sequence) ||
        !ReadUint(json, tokens, json_result->token_count, "issued_epoch", 1U,
                  INT64_MAX, &ticket->issued_epoch) ||
        !ReadUint(json, tokens, json_result->token_count, "expires_epoch", 1U,
                  INT64_MAX, &ticket->expires_epoch) ||
        !SafeIdentifier(ticket->prepare_id, kMaximumPrepareIdBytes, false) ||
        !SafeIdentifier(ticket->tag, kMaximumGitHubReleaseTagBytes, true) ||
        !SafeIdentifier(ticket->github_app_client_id,
                        kMaximumGitHubAppClientIdBytes, false) ||
        !SafeIdentifier(ticket->signing_key_id,
                        kMaximumSigningKeyIdBytes, false) ||
        !DecodeHexDigest(digest, ticket->manifest_sha256)) {
        return DraftTestTicketStatus::FieldsInvalid;
    }
    if (strcmp(ticket->repository, "kdre/bmx") != 0) {
        return DraftTestTicketStatus::RepositoryForbidden;
    }
    if (ticket->expires_epoch <= ticket->issued_epoch ||
        ticket->expires_epoch - ticket->issued_epoch >
            kMaximumDraftTestTicketLifetimeSeconds) {
        return DraftTestTicketStatus::LifetimeInvalid;
    }

    ReleaseSignature signatures[kMaximumReleaseSignatures];
    size_t signature_count = 0U;
    if (ParseSignatureEnvelope(signature_envelope, signatures,
                               kMaximumReleaseSignatures, &signature_count) !=
        SignatureEnvelopeStatus::Ok) {
        return DraftTestTicketStatus::EnvelopeInvalid;
    }
    if (signature_count != 1U ||
        strcmp(signatures[0].key_id, ticket->signing_key_id) != 0) {
        return DraftTestTicketStatus::SigningKeyMismatch;
    }
    const TrustedReleaseKey *trusted = 0;
    for (size_t index = 0U; index < trusted_key_count; ++index) {
        if (!trusted_keys[index].revoked && trusted_keys[index].key_id != 0 &&
            strcmp(trusted_keys[index].key_id, ticket->signing_key_id) == 0) {
            trusted = &trusted_keys[index];
            break;
        }
    }
    if (trusted == 0 || trusted->public_key_pem == 0 ||
        trusted->public_key_pem_size == 0U) {
        return DraftTestTicketStatus::NoTrustedKey;
    }
    if (!verify_function(ByteView(trusted->public_key_pem,
                                  trusted->public_key_pem_size),
                         ticket_bytes,
                         ByteView(signatures[0].der, signatures[0].der_size),
                         verify_context)) {
        return DraftTestTicketStatus::SignatureMismatch;
    }
    if (current_epoch != 0U && current_epoch > ticket->expires_epoch) {
        return DraftTestTicketStatus::Expired;
    }
    return DraftTestTicketStatus::Ok;
}

const char *DraftTestTicketStatusString(DraftTestTicketStatus status) {
    switch (status) {
    case DraftTestTicketStatus::Ok: return "ok";
    case DraftTestTicketStatus::InvalidArgument: return "invalid argument";
    case DraftTestTicketStatus::InvalidSize: return "ticket size/format invalid";
    case DraftTestTicketStatus::JsonInvalid: return "ticket JSON invalid";
    case DraftTestTicketStatus::StorageTooSmall: return "ticket parser storage too small";
    case DraftTestTicketStatus::FieldsInvalid: return "ticket fields invalid";
    case DraftTestTicketStatus::RepositoryForbidden: return "ticket repository forbidden";
    case DraftTestTicketStatus::LifetimeInvalid: return "ticket lifetime invalid";
    case DraftTestTicketStatus::EnvelopeInvalid: return "ticket signature envelope invalid";
    case DraftTestTicketStatus::SigningKeyMismatch: return "ticket signing key mismatch";
    case DraftTestTicketStatus::NoTrustedKey: return "ticket signing key untrusted";
    case DraftTestTicketStatus::SignatureMismatch: return "ticket signature mismatch";
    case DraftTestTicketStatus::Expired: return "ticket expired";
    }
    return "unknown ticket error";
}

}  // namespace update
}  // namespace bmx
