#ifndef BMX_UPDATE_GITHUB_DRAFT_CLIENT_H
#define BMX_UPDATE_GITHUB_DRAFT_CLIENT_H

#include "update/draft_test_ticket.h"
#include "update/github_release_client.h"

namespace bmx {
namespace update {

static const size_t kMaximumGitHubDeviceCodeBytes = 128U;
static const size_t kMaximumGitHubUserCodeBytes = 16U;
static const size_t kMaximumGitHubAccessTokenBytes = 512U;
static const size_t kMaximumGitHubDeviceResponseBytes = 4096U;

struct GitHubDeviceAuthorization {
    char device_code[kMaximumGitHubDeviceCodeBytes + 1U];
    char user_code[kMaximumGitHubUserCodeBytes + 1U];
    uint32_t expires_in_seconds;
    uint32_t polling_interval_seconds;
};

enum class GitHubDeviceFlowStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    DownloadFailed,
    ResponseInvalid,
    AuthorizationPending,
    SlowDown,
    AuthorizationDenied,
    CodeExpired,
    TokenRejected,
    Canceled
};

struct GitHubDeviceFlowResult {
    GitHubDeviceFlowStatus status;
    HttpsGetResult https_result;
    JsonParseResult json_result;
};

GitHubDeviceFlowResult RequestGitHubDeviceCode(
    const char *client_id, SecureStreamFactory *stream_factory,
    HttpsGetWorkspace *https_workspace, uint8_t *response,
    size_t response_capacity, JsonToken *tokens, size_t token_capacity,
    GitHubDeviceAuthorization *authorization,
    UpdateForegroundProgress *foreground_progress = 0);

GitHubDeviceFlowResult ExchangeGitHubDeviceCode(
    const char *client_id, const char *device_code,
    SecureStreamFactory *stream_factory, HttpsGetWorkspace *https_workspace,
    uint8_t *response, size_t response_capacity, JsonToken *tokens,
    size_t token_capacity, char *access_token, size_t access_token_capacity,
    UpdateForegroundProgress *foreground_progress = 0);

ReleaseClientResult CheckPreparedDraftRelease(
    const UpdateStartContext &start_context,
    const DraftTestTicket &ticket,
    const char *access_token,
    ByteView installed_build_info,
    BoardFamily running_board,
    const TrustedReleaseKey *trusted_keys,
    size_t trusted_key_count,
    VerifyP256SignatureFunction verify_function,
    void *verify_context,
    SecureStreamFactory *stream_factory,
    HttpsGetWorkspace *https_workspace,
    const ReleaseClientBuffers &buffers,
    const ReleaseOfferStorage &storage,
    ValidatedReleaseDownload *download,
    UpdateForegroundProgress *foreground_progress = 0);

const char *GitHubDeviceFlowStatusString(GitHubDeviceFlowStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_GITHUB_DRAFT_CLIENT_H
