#include "update/github_draft_client.h"

#include "update/body_sinks.h"
#include "update/github_repository_policy.h"
#include "update/sha256.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

static const char kDeviceCodeUrl[] = "https://github.com/login/device/code";
static const char kAccessTokenUrl[] =
    "https://github.com/login/oauth/access_token";

GitHubDeviceFlowResult DeviceResult(GitHubDeviceFlowStatus status) {
    GitHubDeviceFlowResult result;
    memset(&result, 0, sizeof(result));
    result.status = status;
    result.https_result.status = HttpsGetStatus::InvalidArgument;
    result.json_result.error = JSON_ERROR_ARGUMENT;
    return result;
}

ReleaseClientResult ClientResult() {
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

bool SafeFormValue(const char *value, size_t maximum) {
    if (value == 0) return false;
    size_t size = 0U;
    while (size <= maximum && value[size] != '\0') {
        const char c = value[size];
        const bool safe = (c >= '0' && c <= '9') ||
                          (c >= 'A' && c <= 'Z') ||
                          (c >= 'a' && c <= 'z') || c == '.' || c == '_' ||
                          c == '-';
        if (!safe) return false;
        ++size;
    }
    return size != 0U && size <= maximum;
}

bool CopyRequiredString(const char *json, const JsonToken *tokens,
                        size_t token_count, const char *name, char *output,
                        size_t output_size) {
    const int member = JsonFindObjectMember(json, tokens, token_count, 0, name);
    return member >= 0 && JsonCopyString(json, tokens[member], output,
                                         output_size) == JSON_OK;
}

bool ReadRequiredUint(const char *json, const JsonToken *tokens,
                      size_t token_count, const char *name, uint64_t minimum,
                      uint64_t maximum, uint64_t *output) {
    const int member = JsonFindObjectMember(json, tokens, token_count, 0, name);
    uint64_t value = 0U;
    if (member < 0 || JsonGetUint64(json, tokens[member], &value) != JSON_OK ||
        value < minimum || value > maximum) return false;
    *output = value;
    return true;
}

bool ParseRoot(ByteView encoded, JsonToken *tokens, size_t token_capacity,
               JsonParseResult *result) {
    if (encoded.data == 0 || encoded.size == 0U || tokens == 0 ||
        token_capacity == 0U || result == 0) return false;
    *result = ParseJson(reinterpret_cast<const char *>(encoded.data),
                        encoded.size, tokens, token_capacity, 8U);
    return result->error == JSON_OK && result->token_count != 0U &&
           tokens[0].type == JSON_TOKEN_OBJECT;
}

bool AssetFits(const GitHubReleaseAsset &asset, size_t capacity,
               size_t maximum) {
    return asset.size != 0U && asset.size <= capacity &&
           asset.size <= maximum && asset.size <= SIZE_MAX;
}

HttpsGetResult PostForm(const char *url, const char *body,
                        uint64_t maximum_response_bytes, HttpBodySink *sink,
                        SecureStreamFactory *factory,
                        HttpsGetWorkspace *workspace,
                        UpdateForegroundProgress *foreground_progress) {
    HttpsRequestOptions options;
    options.method = "POST";
    options.accept = "application/json";
    options.content_type = "application/x-www-form-urlencoded";
    options.body = ByteView(reinterpret_cast<const uint8_t *>(body),
                            strlen(body));
    return HttpsRequest(
        url, UpdateUrlPurpose::GitHubDeviceAuthorization,
        HttpsRequestKind::GitHubDeviceFlow, options, maximum_response_bytes,
        sink, factory, workspace, foreground_progress);
}

HttpsGetResult AuthenticatedGet(
    const char *url, UpdateUrlPurpose purpose, HttpsRequestKind kind,
    const char *access_token, uint64_t maximum_body_bytes, HttpBodySink *sink,
    SecureStreamFactory *factory, HttpsGetWorkspace *workspace,
    UpdateForegroundProgress *foreground_progress) {
    HttpsRequestOptions options;
    options.accept = kind == HttpsRequestKind::GitHubApi
                         ? "application/vnd.github+json"
                         : "application/octet-stream";
    options.bearer_token = access_token;
    return HttpsRequest(url, purpose, kind, options, maximum_body_bytes, sink,
                        factory, workspace, foreground_progress);
}

}  // namespace

GitHubDeviceFlowResult RequestGitHubDeviceCode(
    const char *client_id, SecureStreamFactory *stream_factory,
    HttpsGetWorkspace *https_workspace, uint8_t *response,
    size_t response_capacity, JsonToken *tokens, size_t token_capacity,
    GitHubDeviceAuthorization *authorization,
    UpdateForegroundProgress *foreground_progress) {
    GitHubDeviceFlowResult result =
        DeviceResult(GitHubDeviceFlowStatus::InvalidArgument);
    if (!SafeFormValue(client_id, kMaximumGitHubAppClientIdBytes) ||
        stream_factory == 0 || https_workspace == 0 || response == 0 ||
        response_capacity < kMaximumGitHubDeviceResponseBytes || tokens == 0 ||
        token_capacity == 0U || authorization == 0) return result;
    memset(authorization, 0, sizeof(*authorization));
    char body[96U];
    const int body_size = snprintf(body, sizeof(body), "client_id=%s", client_id);
    if (body_size < 0 || static_cast<size_t>(body_size) >= sizeof(body)) {
        return result;
    }
    BoundedMemoryBodySink sink(response, response_capacity);
    result.https_result = PostForm(
        kDeviceCodeUrl, body, kMaximumGitHubDeviceResponseBytes, &sink,
        stream_factory, https_workspace, foreground_progress);
    memset(body, 0, sizeof(body));
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
                            ? GitHubDeviceFlowStatus::Canceled
                            : GitHubDeviceFlowStatus::DownloadFailed;
        return result;
    }
    if (!ParseRoot(sink.bytes(), tokens, token_capacity, &result.json_result)) {
        result.status = GitHubDeviceFlowStatus::ResponseInvalid;
        return result;
    }
    const char *json = reinterpret_cast<const char *>(sink.bytes().data);
    char verification_uri[96U];
    uint64_t expires = 0U;
    uint64_t interval = 0U;
    if (!CopyRequiredString(json, tokens, result.json_result.token_count,
                            "device_code", authorization->device_code,
                            sizeof(authorization->device_code)) ||
        !CopyRequiredString(json, tokens, result.json_result.token_count,
                            "user_code", authorization->user_code,
                            sizeof(authorization->user_code)) ||
        !CopyRequiredString(json, tokens, result.json_result.token_count,
                            "verification_uri", verification_uri,
                            sizeof(verification_uri)) ||
        strcmp(verification_uri, "https://github.com/login/device") != 0 ||
        !ReadRequiredUint(json, tokens, result.json_result.token_count,
                          "expires_in", 1U, 3600U, &expires) ||
        !ReadRequiredUint(json, tokens, result.json_result.token_count,
                          "interval", 1U, 60U, &interval) ||
        !SafeFormValue(authorization->device_code,
                       kMaximumGitHubDeviceCodeBytes) ||
        !SafeFormValue(authorization->user_code,
                       kMaximumGitHubUserCodeBytes)) {
        memset(authorization, 0, sizeof(*authorization));
        result.status = GitHubDeviceFlowStatus::ResponseInvalid;
        return result;
    }
    authorization->expires_in_seconds = static_cast<uint32_t>(expires);
    authorization->polling_interval_seconds = static_cast<uint32_t>(interval);
    result.status = GitHubDeviceFlowStatus::Ok;
    return result;
}

GitHubDeviceFlowResult ExchangeGitHubDeviceCode(
    const char *client_id, const char *device_code,
    SecureStreamFactory *stream_factory, HttpsGetWorkspace *https_workspace,
    uint8_t *response, size_t response_capacity, JsonToken *tokens,
    size_t token_capacity, char *access_token, size_t access_token_capacity,
    UpdateForegroundProgress *foreground_progress) {
    GitHubDeviceFlowResult result =
        DeviceResult(GitHubDeviceFlowStatus::InvalidArgument);
    if (!SafeFormValue(client_id, kMaximumGitHubAppClientIdBytes) ||
        !SafeFormValue(device_code, kMaximumGitHubDeviceCodeBytes) ||
        stream_factory == 0 || https_workspace == 0 || response == 0 ||
        response_capacity < kMaximumGitHubDeviceResponseBytes || tokens == 0 ||
        token_capacity == 0U || access_token == 0 ||
        access_token_capacity < kMaximumGitHubAccessTokenBytes + 1U) {
        return result;
    }
    access_token[0] = '\0';
    char body[384U];
    const int body_size = snprintf(
        body, sizeof(body),
        "client_id=%s&device_code=%s&grant_type="
        "urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
        client_id, device_code);
    if (body_size < 0 || static_cast<size_t>(body_size) >= sizeof(body)) {
        return result;
    }
    BoundedMemoryBodySink sink(response, response_capacity);
    result.https_result = PostForm(
        kAccessTokenUrl, body, kMaximumGitHubDeviceResponseBytes, &sink,
        stream_factory, https_workspace, foreground_progress);
    memset(body, 0, sizeof(body));
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
                            ? GitHubDeviceFlowStatus::Canceled
                            : GitHubDeviceFlowStatus::DownloadFailed;
        return result;
    }
    if (!ParseRoot(sink.bytes(), tokens, token_capacity, &result.json_result)) {
        result.status = GitHubDeviceFlowStatus::ResponseInvalid;
        return result;
    }
    const char *json = reinterpret_cast<const char *>(sink.bytes().data);
    const int error = JsonFindObjectMember(
        json, tokens, result.json_result.token_count, 0, "error");
    if (error >= 0) {
        char value[64U];
        if (JsonCopyString(json, tokens[error], value, sizeof(value)) !=
            JSON_OK) {
            result.status = GitHubDeviceFlowStatus::ResponseInvalid;
        } else if (strcmp(value, "authorization_pending") == 0) {
            result.status = GitHubDeviceFlowStatus::AuthorizationPending;
        } else if (strcmp(value, "slow_down") == 0) {
            result.status = GitHubDeviceFlowStatus::SlowDown;
        } else if (strcmp(value, "access_denied") == 0) {
            result.status = GitHubDeviceFlowStatus::AuthorizationDenied;
        } else if (strcmp(value, "expired_token") == 0) {
            result.status = GitHubDeviceFlowStatus::CodeExpired;
        } else {
            result.status = GitHubDeviceFlowStatus::TokenRejected;
        }
        return result;
    }
    char token_type[16U];
    uint64_t expires = 0U;
    if (!CopyRequiredString(json, tokens, result.json_result.token_count,
                            "access_token", access_token,
                            access_token_capacity) ||
        !CopyRequiredString(json, tokens, result.json_result.token_count,
                            "token_type", token_type, sizeof(token_type)) ||
        strcmp(token_type, "bearer") != 0 ||
        !ReadRequiredUint(json, tokens, result.json_result.token_count,
                          "expires_in", 1U, 28800U, &expires) ||
        !SafeFormValue(access_token, kMaximumGitHubAccessTokenBytes)) {
        memset(access_token, 0, access_token_capacity);
        result.status = GitHubDeviceFlowStatus::TokenRejected;
        return result;
    }
    result.status = GitHubDeviceFlowStatus::Ok;
    return result;
}

ReleaseClientResult CheckPreparedDraftRelease(
    const UpdateStartContext &start_context, const DraftTestTicket &ticket,
    const char *access_token, ByteView installed_build_info,
    BoardFamily running_board, const TrustedReleaseKey *trusted_keys,
    size_t trusted_key_count, VerifyP256SignatureFunction verify_function,
    void *verify_context, SecureStreamFactory *stream_factory,
    HttpsGetWorkspace *https_workspace, const ReleaseClientBuffers &buffers,
    const ReleaseOfferStorage &storage, ValidatedReleaseDownload *download,
    UpdateForegroundProgress *foreground_progress) {
    ReleaseClientResult result = ClientResult();
    if (download == 0 || installed_build_info.data == 0 ||
        installed_build_info.size == 0U || !IsKnownBoardFamily(running_board) ||
        trusted_keys == 0 || trusted_key_count == 0U || verify_function == 0 ||
        stream_factory == 0 || https_workspace == 0 ||
        buffers.github_response == 0 ||
        buffers.github_capacity < kMaximumGitHubReleaseResponseBytes ||
        buffers.manifest == 0 ||
        buffers.manifest_capacity < kMaximumReleaseManifestBytes ||
        buffers.signature == 0 ||
        buffers.signature_capacity < kMaximumSignatureEnvelopeBytes ||
        !SafeFormValue(access_token, kMaximumGitHubAccessTokenBytes) ||
        strcmp(ticket.repository, "kdre/bmx") != 0 || ticket.draft_id == 0U) {
        return result;
    }
    *download = ValidatedReleaseDownload();
    download->installed_build_info = installed_build_info;
    download->acquisition_mode = ReleaseAcquisitionMode::PreparedDraft;
    result.start_decision = EvaluateUpdateStart(start_context);
    if (result.start_decision != UpdateStartDecision::Allowed) {
        result.status = ReleaseClientStatus::InvocationDenied;
        return result;
    }

    char release_url[256U];
    const int release_url_size = snprintf(
        release_url, sizeof(release_url), "https://api.github.com%s%llu",
        kGitHubReleaseApiPathPrefix,
        static_cast<unsigned long long>(ticket.draft_id));
    if (release_url_size < 0 ||
        static_cast<size_t>(release_url_size) >= sizeof(release_url)) {
        return result;
    }
    BoundedMemoryBodySink github_sink(buffers.github_response,
                                      buffers.github_capacity);
    result.https_result = AuthenticatedGet(
        release_url, UpdateUrlPurpose::PreparedDraftDiscovery,
        HttpsRequestKind::GitHubApi, access_token,
        kMaximumGitHubReleaseResponseBytes, &github_sink, stream_factory,
        https_workspace, foreground_progress);
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
    result.github_status = ParseGitHubRelease(
        download->github_response, github_storage,
        GitHubReleaseKind::PreparedDraft, &preliminary, &json_result);
    if (result.github_status != GitHubReleaseParseStatus::Ok ||
        preliminary.id != ticket.draft_id ||
        strcmp(preliminary.tag_name, ticket.tag) != 0) {
        result.status = ReleaseClientStatus::DiscoveryInvalid;
        return result;
    }
    GitHubReleaseMetadataAssets metadata;
    result.assets_status = FindReleaseMetadataAssetsForKind(
        preliminary, GitHubReleaseKind::PreparedDraft, &metadata);
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

    HttpsRequestOptions options;
    options.accept = "application/octet-stream";
    options.bearer_token = access_token;
    BoundedMemoryBodySink manifest_sink(buffers.manifest,
                                        buffers.manifest_capacity);
    result.https_result = HttpsRequest(
        metadata.manifest->api_download_url,
        UpdateUrlPurpose::AuthenticatedReleaseAsset,
        HttpsRequestKind::ReleaseMetadata, options, metadata.manifest->size,
        &manifest_sink, stream_factory, https_workspace, foreground_progress);
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
                            ? ReleaseClientStatus::Canceled
                            : ReleaseClientStatus::ManifestDownloadFailed;
        return result;
    }
    download->manifest = manifest_sink.bytes();
    uint8_t manifest_digest[kSha256DigestBytes];
    if (!Sha256Digest(download->manifest, manifest_digest) ||
        !ConstantTimeDigestEqual(manifest_digest, ticket.manifest_sha256)) {
        memset(manifest_digest, 0, sizeof(manifest_digest));
        result.status = ReleaseClientStatus::OfferInvalid;
        return result;
    }
    memset(manifest_digest, 0, sizeof(manifest_digest));

    BoundedMemoryBodySink signature_sink(buffers.signature,
                                         buffers.signature_capacity);
    result.https_result = HttpsRequest(
        metadata.signature->api_download_url,
        UpdateUrlPurpose::AuthenticatedReleaseAsset,
        HttpsRequestKind::ReleaseMetadata, options, metadata.signature->size,
        &signature_sink, stream_factory, https_workspace, foreground_progress);
    if (result.https_result.status != HttpsGetStatus::Ok) {
        result.status = result.https_result.status == HttpsGetStatus::Canceled
                            ? ReleaseClientStatus::Canceled
                            : ReleaseClientStatus::SignatureDownloadFailed;
        return result;
    }
    download->signature = signature_sink.bytes();

    result.offer_result = BuildReleaseOfferForMode(
        installed_build_info, download->github_response, download->manifest,
        download->signature, running_board,
        ReleaseAcquisitionMode::PreparedDraft, trusted_keys,
        trusted_key_count, verify_function, verify_context, storage,
        &download->offer);
    if (result.offer_result.status != ReleaseOfferStatus::Ok ||
        download->offer.manifest.release_sequence != ticket.release_sequence) {
        result.status = ReleaseClientStatus::OfferInvalid;
        return result;
    }
    result.status = ReleaseClientStatus::Ok;
    return result;
}

const char *GitHubDeviceFlowStatusString(GitHubDeviceFlowStatus status) {
    switch (status) {
    case GitHubDeviceFlowStatus::Ok: return "ok";
    case GitHubDeviceFlowStatus::InvalidArgument: return "invalid argument";
    case GitHubDeviceFlowStatus::DownloadFailed: return "GitHub Device Flow request failed";
    case GitHubDeviceFlowStatus::ResponseInvalid: return "GitHub Device Flow response invalid";
    case GitHubDeviceFlowStatus::AuthorizationPending: return "GitHub authorization still pending";
    case GitHubDeviceFlowStatus::SlowDown: return "GitHub authorization polling too fast";
    case GitHubDeviceFlowStatus::AuthorizationDenied: return "GitHub authorization denied";
    case GitHubDeviceFlowStatus::CodeExpired: return "GitHub device code expired";
    case GitHubDeviceFlowStatus::TokenRejected: return "GitHub access token rejected";
    case GitHubDeviceFlowStatus::Canceled: return "GitHub Device Flow canceled";
    }
    return "unknown GitHub Device Flow error";
}

}  // namespace update
}  // namespace bmx
