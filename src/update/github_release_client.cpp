#include "update/github_release_client.h"

#include "update/signature_envelope.h"

#include <string.h>

namespace bmx {
namespace update {

namespace {

ReleaseClientResult InitialResult() {
    ReleaseClientResult result;
    memset(&result, 0, sizeof(result));
    result.status = ReleaseClientStatus::InvalidArgument;
    result.start_decision = UpdateStartDecision::NotExplicitUserAction;
    result.https_result.status = HttpsGetStatus::InvalidArgument;
    result.github_status = GitHubReleaseParseStatus::InvalidArgument;
    result.assets_status = RequiredGitHubAssetsStatus::InvalidArgument;
    result.offer_result.status = ReleaseOfferStatus::InvalidArgument;
    return result;
}

bool BuffersAreValid(const ReleaseClientBuffers &buffers) {
    return buffers.github_response != 0 &&
           buffers.github_capacity >= kMaximumGitHubReleaseResponseBytes &&
           buffers.manifest != 0 &&
           buffers.manifest_capacity >= kMaximumReleaseManifestBytes &&
           buffers.signature != 0 &&
           buffers.signature_capacity >= kMaximumSignatureEnvelopeBytes;
}

bool AssetFits(const GitHubReleaseAsset &asset, size_t capacity,
               size_t protocol_maximum) {
    return asset.size != 0U && asset.size <= capacity &&
           asset.size <= protocol_maximum && asset.size <= SIZE_MAX;
}

}  // namespace

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
    UpdateForegroundProgress *foreground_progress) {
    ReleaseClientResult result = InitialResult();
    if (download == 0 || installed_build_info.data == 0 ||
        installed_build_info.size == 0U || !IsKnownBoardFamily(running_board) ||
        trusted_keys == 0 || trusted_key_count == 0U || verify_function == 0 ||
        stream_factory == 0 || https_workspace == 0 ||
        !BuffersAreValid(buffers)) {
        return result;
    }
    *download = ValidatedReleaseDownload();
    download->installed_build_info = installed_build_info;
    download->acquisition_mode = ReleaseAcquisitionMode::PublishedStable;

    result.start_decision = EvaluateUpdateStart(start_context);
    if (result.start_decision != UpdateStartDecision::Allowed) {
        result.status = ReleaseClientStatus::InvocationDenied;
        return result;
    }

    BoundedMemoryBodySink github_sink(buffers.github_response,
                                      buffers.github_capacity);
    result.https_result = HttpsGet(
        kGitHubLatestReleaseUrl, UpdateUrlPurpose::Discovery,
        HttpsRequestKind::GitHubApi, kMaximumGitHubReleaseResponseBytes,
        &github_sink, stream_factory, https_workspace, foreground_progress);
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
            ? ReleaseClientStatus::Canceled
            : ReleaseClientStatus::DiscoveryDownloadFailed;
        return result;
    }
    download->github_response = github_sink.bytes();

    GitHubLatestRelease preliminary;
    JsonParseResult json_result;
    const GitHubReleaseParseStorage github_storage = {
        storage.github_tokens, storage.github_token_capacity,
        storage.github_assets, storage.github_asset_capacity};
    result.github_status = ParseGitHubLatestRelease(
        download->github_response, github_storage, &preliminary, &json_result);
    if (result.github_status != GitHubReleaseParseStatus::Ok) {
        result.status = ReleaseClientStatus::DiscoveryInvalid;
        return result;
    }
    GitHubReleaseMetadataAssets metadata;
    result.assets_status = FindReleaseMetadataAssets(preliminary, &metadata);
    if (result.assets_status != RequiredGitHubAssetsStatus::Ok) {
        result.status = ReleaseClientStatus::MetadataAssetsInvalid;
        return result;
    }
    if (!AssetFits(*metadata.manifest, buffers.manifest_capacity,
                   kMaximumReleaseManifestBytes) ||
        !AssetFits(*metadata.signature, buffers.signature_capacity,
                   kMaximumSignatureEnvelopeBytes)) {
        result.status = ReleaseClientStatus::MetadataSizeInvalid;
        return result;
    }

    BoundedMemoryBodySink manifest_sink(buffers.manifest,
                                        buffers.manifest_capacity);
    result.https_result = HttpsGet(
        metadata.manifest->browser_download_url, UpdateUrlPurpose::ReleaseAsset,
        HttpsRequestKind::ReleaseMetadata, metadata.manifest->size,
        &manifest_sink, stream_factory, https_workspace, foreground_progress);
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
            ? ReleaseClientStatus::Canceled
            : ReleaseClientStatus::ManifestDownloadFailed;
        return result;
    }
    download->manifest = manifest_sink.bytes();

    BoundedMemoryBodySink signature_sink(buffers.signature,
                                         buffers.signature_capacity);
    result.https_result = HttpsGet(
        metadata.signature->browser_download_url,
        UpdateUrlPurpose::ReleaseAsset, HttpsRequestKind::ReleaseMetadata,
        metadata.signature->size, &signature_sink, stream_factory,
        https_workspace, foreground_progress);
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
            ? ReleaseClientStatus::Canceled
            : ReleaseClientStatus::SignatureDownloadFailed;
        return result;
    }
    download->signature = signature_sink.bytes();

    result.offer_result = BuildReleaseOffer(
        installed_build_info, download->github_response, download->manifest,
        download->signature, running_board, trusted_keys, trusted_key_count,
        verify_function, verify_context, storage, &download->offer);
    if (result.offer_result.status != ReleaseOfferStatus::Ok) {
        result.status = ReleaseClientStatus::OfferInvalid;
        return result;
    }
    result.status = ReleaseClientStatus::Ok;
    return result;
}

const char *ReleaseClientStatusString(ReleaseClientStatus status) {
    switch (status) {
    case ReleaseClientStatus::Ok: return "ok";
    case ReleaseClientStatus::InvalidArgument: return "invalid argument";
    case ReleaseClientStatus::InvocationDenied: return "update invocation denied";
    case ReleaseClientStatus::DiscoveryDownloadFailed:
        return "GitHub latest-release download failed";
    case ReleaseClientStatus::DiscoveryInvalid:
        return "GitHub latest-release response invalid";
    case ReleaseClientStatus::MetadataAssetsInvalid:
        return "GitHub release metadata assets invalid";
    case ReleaseClientStatus::MetadataSizeInvalid:
        return "GitHub release metadata size invalid";
    case ReleaseClientStatus::ManifestDownloadFailed:
        return "release manifest download failed";
    case ReleaseClientStatus::SignatureDownloadFailed:
        return "release signature download failed";
    case ReleaseClientStatus::OfferInvalid:
        return "release offer authentication or policy failed";
    case ReleaseClientStatus::Canceled:
        return "update check canceled";
    }
    return "unknown release client error";
}

}  // namespace update
}  // namespace bmx
