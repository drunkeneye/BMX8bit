#ifndef BMX_UPDATE_DRAFT_TEST_TICKET_H
#define BMX_UPDATE_DRAFT_TEST_TICKET_H

#include "update/github_release_parser.h"
#include "update/json_parser.h"
#include "update/signature_verifier.h"

namespace bmx {
namespace update {

static const size_t kMaximumDraftTestTicketBytes = 4096U;
static const size_t kMaximumGitHubAppClientIdBytes = 64U;
static const size_t kMaximumPrepareIdBytes = 96U;
static const uint64_t kMaximumDraftTestTicketLifetimeSeconds =
    7U * 24U * 60U * 60U;

struct DraftTestTicket {
    char repository[sizeof("kdre/bmx")];
    char prepare_id[kMaximumPrepareIdBytes + 1U];
    char tag[kMaximumGitHubReleaseTagBytes + 1U];
    char github_app_client_id[kMaximumGitHubAppClientIdBytes + 1U];
    char signing_key_id[kMaximumSigningKeyIdBytes + 1U];
    uint64_t draft_id;
    uint64_t release_sequence;
    uint64_t issued_epoch;
    uint64_t expires_epoch;
    uint8_t manifest_sha256[kSha256DigestBytes];
};

enum class DraftTestTicketStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidSize,
    JsonInvalid,
    StorageTooSmall,
    FieldsInvalid,
    RepositoryForbidden,
    LifetimeInvalid,
    EnvelopeInvalid,
    SigningKeyMismatch,
    NoTrustedKey,
    SignatureMismatch,
    Expired
};

// Parses and authenticates an owner-created production draft ticket.  The
// ticket is a local visibility/identity gate only; GitHub authentication is a
// separate Device Flow and the release manifest is independently verified.
DraftTestTicketStatus VerifyDraftTestTicket(
    ByteView ticket_bytes,
    ByteView signature_envelope,
    JsonToken *tokens,
    size_t token_capacity,
    const TrustedReleaseKey *trusted_keys,
    size_t trusted_key_count,
    VerifyP256SignatureFunction verify_function,
    void *verify_context,
    uint64_t current_epoch,
    DraftTestTicket *ticket,
    JsonParseResult *json_result);

const char *DraftTestTicketStatusString(DraftTestTicketStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_DRAFT_TEST_TICKET_H
