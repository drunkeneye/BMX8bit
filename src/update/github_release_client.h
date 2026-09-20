#ifndef BMX_UPDATE_GITHUB_RELEASE_CLIENT_H
#define BMX_UPDATE_GITHUB_RELEASE_CLIENT_H

#include "update/body_sinks.h"
#include "update/github_repository_policy.h"
#include "update/https_stream.h"
#include "update/release_offer.h"

namespace bmx {
namespace update {

struct ReleaseClientBuffers {
    uint8_t *github_response;
    size_t github_capacity;
    uint8_t *manifest;
    size_t manifest_capacity;
    uint8_t *signature;
    size_t signature_capacity;
};

struct ValidatedReleaseDownload {
    // Exact installed metadata used by BuildReleaseOffer.  The caller-owned
    // bytes must remain alive alongside the downloaded metadata so a later
    // installation boundary can reparse and reverify all raw inputs instead
    // of trusting the freely copyable ReleaseOffer POD below.
    ByteView installed_build_info;
    ReleaseAcquisitionMode acquisition_mode;
    ReleaseOffer offer;
    ByteView github_response;
    ByteView manifest;
    ByteView signature;
};

enum class ReleaseClientStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvocationDenied,
    DiscoveryDownloadFailed,
    DiscoveryInvalid,
    MetadataAssetsInvalid,
    MetadataSizeInvalid,
    ManifestDownloadFailed,
    SignatureDownloadFailed,
    OfferInvalid,
    Canceled
};

struct ReleaseClientResult {
    ReleaseClientStatus status;
    UpdateStartDecision start_decision;
    HttpsGetResult https_result;
    GitHubReleaseParseStatus github_status;
    RequiredGitHubAssetsStatus assets_status;
    ReleaseOfferResult offer_result;
};

// Performs network traffic only when |start_context| identifies the confirmed
// Update menu action and the network feature is already enabled and ready.
// Merely constructing this client is inert.
ReleaseClientResult CheckLatestRelease(
    const UpdateStartContext &start_context,
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

const char *ReleaseClientStatusString(ReleaseClientStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_GITHUB_RELEASE_CLIENT_H
