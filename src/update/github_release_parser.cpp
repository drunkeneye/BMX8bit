#include "update/github_release_parser.h"

#include "update/github_repository_policy.h"

#include <stdio.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

static const char kManifestAssetName[] = "BMX-RELEASE.json";
static const char kSignatureAssetName[] = "BMX-RELEASE.sig";

enum class CopyStringStatus {
    Ok,
    WrongType,
    Empty,
    TooLong,
    UnsafeCharacter
};

CopyStringStatus CopyUnescapedAsciiString(const char *json,
                                           const JsonToken &token,
                                           char *output,
                                           size_t output_size) {
    if (token.type != JSON_TOKEN_STRING) {
        return CopyStringStatus::WrongType;
    }
    const size_t length = static_cast<size_t>(token.end - token.start);
    if (length == 0U) {
        return CopyStringStatus::Empty;
    }
    if (length >= output_size) {
        return CopyStringStatus::TooLong;
    }
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c =
            static_cast<unsigned char>(json[token.start + i]);
        // All values used in a request URL are deliberately constrained to a
        // simple, unescaped ASCII representation. This avoids embedded-NUL,
        // Unicode-normalization and percent-encoding ambiguities.
        if (c < 0x21U || c > 0x7eU || c == '\\') {
            return CopyStringStatus::UnsafeCharacter;
        }
    }
    memcpy(output, json + token.start, length);
    output[length] = '\0';
    return CopyStringStatus::Ok;
}

bool IsSafeIdentifierCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

size_t BoundedLength(const char *value, size_t maximum) {
    if (value == 0) {
        return maximum + 1U;
    }
    size_t length = 0U;
    while (length <= maximum && value[length] != '\0') {
        ++length;
    }
    return length;
}

bool IsSafeIdentifier(const char *value, size_t maximum) {
    const size_t length = BoundedLength(value, maximum);
    if (length == 0U || length > maximum) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (!IsSafeIdentifierCharacter(value[i])) {
            return false;
        }
    }
    return true;
}

bool IsSafeTag(const char *tag) {
    return IsSafeIdentifier(tag, kMaximumGitHubReleaseTagBytes);
}

bool IsSafeAssetName(const char *name) {
    return IsSafeIdentifier(name, kMaximumGitHubAssetNameBytes) &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

bool IsNullTerminatedUrl(const char *url) {
    const size_t length = BoundedLength(url, kMaximumUpdateUrlBytes - 1U);
    return length > 0U && length < kMaximumUpdateUrlBytes;
}

enum class AssetUrlStatus {
    Ok,
    Rejected,
    BindingMismatch
};

AssetUrlStatus ValidateAssetUrl(const GitHubReleaseAsset &asset,
                                const char *tag) {
    if (!IsSafeTag(tag) || !IsSafeAssetName(asset.name) ||
        !IsNullTerminatedUrl(asset.browser_download_url)) {
        return AssetUrlStatus::BindingMismatch;
    }

    ParsedUpdateUrl parsed;
    if (ParseAndAuthorizeUpdateUrl(asset.browser_download_url,
                                   UpdateUrlPurpose::ReleaseAsset,
                                   &parsed) != UrlPolicyStatus::Ok) {
        return AssetUrlStatus::Rejected;
    }

    const size_t prefix_length = strlen(kGitHubReleaseDownloadPathPrefix);
    if (strncmp(parsed.path_and_query, kGitHubReleaseDownloadPathPrefix,
                prefix_length) != 0) {
        return AssetUrlStatus::BindingMismatch;
    }
    const char *url_tag = parsed.path_and_query + prefix_length;
    const char *separator = strchr(url_tag, '/');
    if (separator == 0) {
        return AssetUrlStatus::BindingMismatch;
    }
    const size_t url_tag_length = static_cast<size_t>(separator - url_tag);
    const size_t tag_length = strlen(tag);
    if (url_tag_length != tag_length ||
        memcmp(url_tag, tag, tag_length) != 0 ||
        strcmp(separator + 1, asset.name) != 0) {
        return AssetUrlStatus::BindingMismatch;
    }
    return AssetUrlStatus::Ok;
}

GitHubReleaseParseStatus MapCopyStatus(CopyStringStatus status) {
    if (status == CopyStringStatus::WrongType) {
        return GitHubReleaseParseStatus::WrongType;
    }
    return GitHubReleaseParseStatus::InvalidValue;
}

int RequiredMember(const char *json,
                   const JsonToken *tokens,
                   size_t token_count,
                   int object,
                   const char *name) {
    return JsonFindObjectMember(json, tokens, token_count, object, name);
}

bool SameName(const GitHubReleaseAsset &left,
              const GitHubReleaseAsset &right) {
    return strcmp(left.name, right.name) == 0;
}

void ResetRelease(GitHubLatestRelease *release,
                  GitHubReleaseAsset *asset_storage) {
    release->id = 0U;
    release->tag_name[0] = '\0';
    release->draft = true;
    release->prerelease = true;
    release->assets = asset_storage;
    release->asset_count = 0U;
}

bool EligibleKind(const GitHubLatestRelease &release,
                  GitHubReleaseKind kind) {
    return kind == GitHubReleaseKind::PreparedDraft
               ? release.draft && !release.prerelease
               : !release.draft && !release.prerelease;
}

bool ValidateApiAssetUrl(const GitHubReleaseAsset &asset) {
    ParsedUpdateUrl parsed;
    return asset.id != 0U && asset.api_download_url[0] != '\0' &&
           ParseAndAuthorizeUpdateUrl(
               asset.api_download_url,
               UpdateUrlPurpose::AuthenticatedReleaseAsset, &parsed) ==
               UrlPolicyStatus::Ok;
}

AssetUrlStatus ValidateAssetUrlForKind(const GitHubReleaseAsset &asset,
                                       const char *tag,
                                       GitHubReleaseKind kind) {
    return kind == GitHubReleaseKind::PreparedDraft
               ? (ValidateApiAssetUrl(asset) ? AssetUrlStatus::Ok
                                             : AssetUrlStatus::BindingMismatch)
               : ValidateAssetUrl(asset, tag);
}

void ResetRequired(RequiredGitHubReleaseAssets *required) {
    required->manifest = 0;
    required->signature = 0;
    required->board_zip = 0;
}

}  // namespace

RequiredGitHubAssetsStatus FindReleaseMetadataAssets(
    const GitHubLatestRelease &release,
    GitHubReleaseMetadataAssets *metadata) {
    return FindReleaseMetadataAssetsForKind(
        release, GitHubReleaseKind::PublishedStable, metadata);
}

RequiredGitHubAssetsStatus FindReleaseMetadataAssetsForKind(
    const GitHubLatestRelease &release, GitHubReleaseKind kind,
    GitHubReleaseMetadataAssets *metadata) {
    if (metadata == 0) {
        return RequiredGitHubAssetsStatus::InvalidArgument;
    }
    metadata->manifest = 0;
    metadata->signature = 0;
    if (release.assets == 0) {
        return RequiredGitHubAssetsStatus::InvalidArgument;
    }
    if (!EligibleKind(release, kind)) {
        return RequiredGitHubAssetsStatus::IneligibleRelease;
    }
    if (!IsSafeTag(release.tag_name)) {
        return RequiredGitHubAssetsStatus::InvalidSignedTag;
    }
    if (release.asset_count > kMaximumGitHubReleaseAssets) {
        return RequiredGitHubAssetsStatus::TooManyAssets;
    }
    for (size_t i = 0U; i < release.asset_count; ++i) {
        const GitHubReleaseAsset &asset = release.assets[i];
        if (!IsSafeAssetName(asset.name) ||
            !IsNullTerminatedUrl(asset.browser_download_url) ||
            (kind == GitHubReleaseKind::PreparedDraft &&
             !ValidateApiAssetUrl(asset))) {
            return RequiredGitHubAssetsStatus::UrlBindingMismatch;
        }
        for (size_t earlier = 0U; earlier < i; ++earlier) {
            if (strcmp(release.assets[earlier].name, asset.name) == 0) {
                return RequiredGitHubAssetsStatus::DuplicateAssetName;
            }
        }
        const AssetUrlStatus url_status =
            ValidateAssetUrlForKind(asset, release.tag_name, kind);
        if (url_status == AssetUrlStatus::Rejected) {
            return RequiredGitHubAssetsStatus::UrlRejected;
        }
        if (url_status != AssetUrlStatus::Ok) {
            return RequiredGitHubAssetsStatus::UrlBindingMismatch;
        }
        if (strcmp(asset.name, kManifestAssetName) == 0) {
            metadata->manifest = &asset;
        } else if (strcmp(asset.name, kSignatureAssetName) == 0) {
            metadata->signature = &asset;
        }
    }
    if (metadata->manifest == 0) {
        return RequiredGitHubAssetsStatus::MissingManifest;
    }
    if (metadata->signature == 0) {
        return RequiredGitHubAssetsStatus::MissingSignature;
    }
    return RequiredGitHubAssetsStatus::Ok;
}

GitHubReleaseParseStatus ParseGitHubLatestRelease(
    ByteView encoded,
    const GitHubReleaseParseStorage &storage,
    GitHubLatestRelease *release,
    JsonParseResult *json_result) {
    return ParseGitHubRelease(encoded, storage,
                              GitHubReleaseKind::PublishedStable, release,
                              json_result);
}

GitHubReleaseParseStatus ParseGitHubRelease(
    ByteView encoded, const GitHubReleaseParseStorage &storage,
    GitHubReleaseKind kind, GitHubLatestRelease *release,
    JsonParseResult *json_result) {
    if (json_result != 0) {
        json_result->error = JSON_ERROR_ARGUMENT;
        json_result->token_count = 0U;
        json_result->error_offset = 0U;
    }
    if (release == 0 || encoded.data == 0 || encoded.size == 0U ||
        storage.tokens == 0 || storage.assets == 0) {
        return GitHubReleaseParseStatus::InvalidArgument;
    }
    ResetRelease(release, storage.assets);
    if (encoded.size > kMaximumGitHubReleaseResponseBytes) {
        return GitHubReleaseParseStatus::ResponseTooLarge;
    }
    if (storage.token_capacity == 0U || storage.asset_capacity == 0U) {
        return GitHubReleaseParseStatus::StorageTooSmall;
    }

    const char *json = reinterpret_cast<const char *>(encoded.data);
    const JsonParseResult parsed = ParseJson(
        json, encoded.size, storage.tokens, storage.token_capacity, 32U);
    if (json_result != 0) {
        *json_result = parsed;
    }
    if (parsed.error == JSON_ERROR_TOKEN_LIMIT) {
        return GitHubReleaseParseStatus::StorageTooSmall;
    }
    if (parsed.error != JSON_OK) {
        return GitHubReleaseParseStatus::JsonInvalid;
    }
    if (parsed.token_count == 0U ||
        storage.tokens[0].type != JSON_TOKEN_OBJECT) {
        return GitHubReleaseParseStatus::WrongRootType;
    }

    const int release_id = RequiredMember(
        json, storage.tokens, parsed.token_count, 0, "id");
    const int tag = RequiredMember(json, storage.tokens, parsed.token_count,
                                   0, "tag_name");
    const int draft = RequiredMember(json, storage.tokens, parsed.token_count,
                                     0, "draft");
    const int prerelease = RequiredMember(json, storage.tokens,
                                          parsed.token_count, 0,
                                          "prerelease");
    const int assets = RequiredMember(json, storage.tokens, parsed.token_count,
                                      0, "assets");
    if (tag < 0 || draft < 0 || prerelease < 0 || assets < 0 ||
        (kind == GitHubReleaseKind::PreparedDraft && release_id < 0)) {
        return GitHubReleaseParseStatus::MissingField;
    }

    if (release_id >= 0 &&
        (JsonGetUint64(json, storage.tokens[release_id], &release->id) !=
             JSON_OK ||
         release->id == 0U)) {
        return storage.tokens[release_id].type == JSON_TOKEN_NUMBER
                   ? GitHubReleaseParseStatus::InvalidValue
                   : GitHubReleaseParseStatus::WrongType;
    }

    const CopyStringStatus tag_copy = CopyUnescapedAsciiString(
        json, storage.tokens[tag], release->tag_name,
        sizeof(release->tag_name));
    if (tag_copy != CopyStringStatus::Ok) {
        return MapCopyStatus(tag_copy);
    }
    if (!IsSafeTag(release->tag_name)) {
        return GitHubReleaseParseStatus::InvalidValue;
    }
    if (JsonGetBool(storage.tokens[draft], &release->draft) != JSON_OK ||
        JsonGetBool(storage.tokens[prerelease], &release->prerelease) !=
            JSON_OK) {
        return GitHubReleaseParseStatus::WrongType;
    }
    if (storage.tokens[assets].type != JSON_TOKEN_ARRAY) {
        return GitHubReleaseParseStatus::WrongType;
    }
    if (release->draft && kind != GitHubReleaseKind::PreparedDraft) {
        return GitHubReleaseParseStatus::DraftForbidden;
    }
    if (release->prerelease) {
        return GitHubReleaseParseStatus::PrereleaseForbidden;
    }
    if (kind == GitHubReleaseKind::PreparedDraft && !release->draft) {
        return GitHubReleaseParseStatus::DraftForbidden;
    }

    const size_t asset_count = storage.tokens[assets].child_count;
    if (asset_count > kMaximumGitHubReleaseAssets) {
        return GitHubReleaseParseStatus::TooManyAssets;
    }
    if (asset_count > storage.asset_capacity) {
        return GitHubReleaseParseStatus::StorageTooSmall;
    }

    size_t output_index = 0U;
    for (size_t token_index = static_cast<size_t>(assets) + 1U;
         token_index < parsed.token_count && output_index < asset_count;
         ++token_index) {
        if (storage.tokens[token_index].parent != assets) {
            continue;
        }
        if (storage.tokens[token_index].type != JSON_TOKEN_OBJECT) {
            return GitHubReleaseParseStatus::WrongType;
        }
        const int asset_object = static_cast<int>(token_index);
        const int name = RequiredMember(json, storage.tokens,
                                        parsed.token_count, asset_object,
                                        "name");
        const int id = RequiredMember(json, storage.tokens,
                                      parsed.token_count, asset_object,
                                      "id");
        const int size = RequiredMember(json, storage.tokens,
                                        parsed.token_count, asset_object,
                                        "size");
        const int url = RequiredMember(json, storage.tokens,
                                       parsed.token_count, asset_object,
                                       "browser_download_url");
        if (name < 0 || size < 0 || url < 0 ||
            (kind == GitHubReleaseKind::PreparedDraft && id < 0)) {
            return GitHubReleaseParseStatus::MissingField;
        }

        GitHubReleaseAsset &output = storage.assets[output_index];
        memset(&output, 0, sizeof(output));
        if (id >= 0 &&
            (JsonGetUint64(json, storage.tokens[id], &output.id) != JSON_OK ||
             output.id == 0U)) {
            return storage.tokens[id].type == JSON_TOKEN_NUMBER
                       ? GitHubReleaseParseStatus::InvalidValue
                       : GitHubReleaseParseStatus::WrongType;
        }
        const CopyStringStatus name_copy = CopyUnescapedAsciiString(
            json, storage.tokens[name], output.name, sizeof(output.name));
        if (name_copy != CopyStringStatus::Ok) {
            return MapCopyStatus(name_copy);
        }
        if (!IsSafeAssetName(output.name)) {
            return GitHubReleaseParseStatus::InvalidValue;
        }
        if (JsonGetUint64(json, storage.tokens[size], &output.size) !=
            JSON_OK) {
            return storage.tokens[size].type == JSON_TOKEN_NUMBER
                       ? GitHubReleaseParseStatus::InvalidValue
                       : GitHubReleaseParseStatus::WrongType;
        }
        const CopyStringStatus url_copy = CopyUnescapedAsciiString(
            json, storage.tokens[url], output.browser_download_url,
            sizeof(output.browser_download_url));
        if (url_copy != CopyStringStatus::Ok) {
            return MapCopyStatus(url_copy);
        }
        if (output.id != 0U) {
            const int api_size = snprintf(
                output.api_download_url, sizeof(output.api_download_url),
                "https://api.github.com%s%llu",
                kGitHubReleaseAssetApiPathPrefix,
                static_cast<unsigned long long>(output.id));
            if (api_size < 0 || static_cast<size_t>(api_size) >=
                                     sizeof(output.api_download_url) ||
                !ValidateApiAssetUrl(output)) {
                return GitHubReleaseParseStatus::UrlBindingMismatch;
            }
        }

        for (size_t earlier = 0U; earlier < output_index; ++earlier) {
            if (SameName(storage.assets[earlier], output)) {
                return GitHubReleaseParseStatus::DuplicateAssetName;
            }
        }
        const AssetUrlStatus url_status =
            ValidateAssetUrlForKind(output, release->tag_name, kind);
        if (url_status == AssetUrlStatus::Rejected) {
            return GitHubReleaseParseStatus::UrlRejected;
        }
        if (url_status != AssetUrlStatus::Ok) {
            return GitHubReleaseParseStatus::UrlBindingMismatch;
        }
        ++output_index;
    }
    if (output_index != asset_count) {
        return GitHubReleaseParseStatus::JsonInvalid;
    }
    release->asset_count = output_index;
    return GitHubReleaseParseStatus::Ok;
}

RequiredGitHubAssetsStatus FindRequiredAssets(
    const GitHubLatestRelease &release,
    const char *signed_manifest_tag,
    const char *signed_board_zip_name,
    RequiredGitHubReleaseAssets *required) {
    return FindRequiredAssetsForKind(
        release, GitHubReleaseKind::PublishedStable, signed_manifest_tag,
        signed_board_zip_name, required);
}

RequiredGitHubAssetsStatus FindRequiredAssetsForKind(
    const GitHubLatestRelease &release, GitHubReleaseKind kind,
    const char *signed_manifest_tag, const char *signed_board_zip_name,
    RequiredGitHubReleaseAssets *required) {
    if (required == 0) {
        return RequiredGitHubAssetsStatus::InvalidArgument;
    }
    ResetRequired(required);
    if (release.assets == 0 || signed_manifest_tag == 0 ||
        signed_board_zip_name == 0) {
        return RequiredGitHubAssetsStatus::InvalidArgument;
    }
    if (!EligibleKind(release, kind)) {
        return RequiredGitHubAssetsStatus::IneligibleRelease;
    }
    if (!IsSafeTag(signed_manifest_tag)) {
        return RequiredGitHubAssetsStatus::InvalidSignedTag;
    }
    if (!IsSafeAssetName(signed_board_zip_name) ||
        strcmp(signed_board_zip_name, kManifestAssetName) == 0 ||
        strcmp(signed_board_zip_name, kSignatureAssetName) == 0) {
        return RequiredGitHubAssetsStatus::InvalidBoardZipName;
    }
    if (!IsSafeTag(release.tag_name) ||
        strcmp(release.tag_name, signed_manifest_tag) != 0) {
        return RequiredGitHubAssetsStatus::SignedTagMismatch;
    }
    if (release.asset_count > kMaximumGitHubReleaseAssets) {
        return RequiredGitHubAssetsStatus::TooManyAssets;
    }

    for (size_t i = 0U; i < release.asset_count; ++i) {
        const GitHubReleaseAsset &asset = release.assets[i];
        if (!IsSafeAssetName(asset.name) ||
            !IsNullTerminatedUrl(asset.browser_download_url) ||
            (kind == GitHubReleaseKind::PreparedDraft &&
             !ValidateApiAssetUrl(asset))) {
            return RequiredGitHubAssetsStatus::UrlBindingMismatch;
        }
        for (size_t earlier = 0U; earlier < i; ++earlier) {
            if (strcmp(release.assets[earlier].name, asset.name) == 0) {
                return RequiredGitHubAssetsStatus::DuplicateAssetName;
            }
        }
        const AssetUrlStatus url_status =
            ValidateAssetUrlForKind(asset, signed_manifest_tag, kind);
        if (url_status == AssetUrlStatus::Rejected) {
            return RequiredGitHubAssetsStatus::UrlRejected;
        }
        if (url_status != AssetUrlStatus::Ok) {
            return RequiredGitHubAssetsStatus::UrlBindingMismatch;
        }
        if (strcmp(asset.name, kManifestAssetName) == 0) {
            required->manifest = &asset;
        } else if (strcmp(asset.name, kSignatureAssetName) == 0) {
            required->signature = &asset;
        } else if (strcmp(asset.name, signed_board_zip_name) == 0) {
            required->board_zip = &asset;
        }
    }

    if (required->manifest == 0) {
        return RequiredGitHubAssetsStatus::MissingManifest;
    }
    if (required->signature == 0) {
        return RequiredGitHubAssetsStatus::MissingSignature;
    }
    if (required->board_zip == 0) {
        return RequiredGitHubAssetsStatus::MissingBoardZip;
    }
    return RequiredGitHubAssetsStatus::Ok;
}

const char *GitHubAssetDownloadUrl(const GitHubReleaseAsset &asset,
                                   GitHubReleaseKind kind) {
    return kind == GitHubReleaseKind::PreparedDraft
               ? asset.api_download_url : asset.browser_download_url;
}

const char *GitHubReleaseParseStatusString(GitHubReleaseParseStatus status) {
    switch (status) {
        case GitHubReleaseParseStatus::Ok: return "ok";
        case GitHubReleaseParseStatus::InvalidArgument:
            return "invalid argument";
        case GitHubReleaseParseStatus::ResponseTooLarge:
            return "GitHub response too large";
        case GitHubReleaseParseStatus::StorageTooSmall:
            return "GitHub parser storage too small";
        case GitHubReleaseParseStatus::JsonInvalid:
            return "invalid GitHub JSON";
        case GitHubReleaseParseStatus::WrongRootType:
            return "GitHub response root is not an object";
        case GitHubReleaseParseStatus::MissingField:
            return "required GitHub field missing";
        case GitHubReleaseParseStatus::WrongType:
            return "wrong GitHub field type";
        case GitHubReleaseParseStatus::InvalidValue:
            return "invalid GitHub field value";
        case GitHubReleaseParseStatus::TooManyAssets:
            return "too many GitHub release assets";
        case GitHubReleaseParseStatus::DuplicateAssetName:
            return "duplicate GitHub release asset name";
        case GitHubReleaseParseStatus::UrlRejected:
            return "GitHub release asset URL rejected";
        case GitHubReleaseParseStatus::UrlBindingMismatch:
            return "GitHub asset URL does not match tag and asset name";
        case GitHubReleaseParseStatus::DraftForbidden:
            return "GitHub draft release forbidden";
        case GitHubReleaseParseStatus::PrereleaseForbidden:
            return "GitHub prerelease forbidden";
    }
    return "unknown GitHub release parse error";
}

const char *RequiredGitHubAssetsStatusString(
    RequiredGitHubAssetsStatus status) {
    switch (status) {
        case RequiredGitHubAssetsStatus::Ok: return "ok";
        case RequiredGitHubAssetsStatus::InvalidArgument:
            return "invalid argument";
        case RequiredGitHubAssetsStatus::IneligibleRelease:
            return "GitHub release is a draft or prerelease";
        case RequiredGitHubAssetsStatus::InvalidSignedTag:
            return "invalid signed release tag";
        case RequiredGitHubAssetsStatus::InvalidBoardZipName:
            return "invalid signed board ZIP name";
        case RequiredGitHubAssetsStatus::SignedTagMismatch:
            return "GitHub and signed manifest tags differ";
        case RequiredGitHubAssetsStatus::TooManyAssets:
            return "too many GitHub release assets";
        case RequiredGitHubAssetsStatus::DuplicateAssetName:
            return "duplicate GitHub release asset name";
        case RequiredGitHubAssetsStatus::UrlRejected:
            return "GitHub release asset URL rejected";
        case RequiredGitHubAssetsStatus::UrlBindingMismatch:
            return "GitHub asset URL does not match signed tag and name";
        case RequiredGitHubAssetsStatus::MissingManifest:
            return "BMX-RELEASE.json missing";
        case RequiredGitHubAssetsStatus::MissingSignature:
            return "BMX-RELEASE.sig missing";
        case RequiredGitHubAssetsStatus::MissingBoardZip:
            return "signed board ZIP missing";
    }
    return "unknown required GitHub asset error";
}

}  // namespace update
}  // namespace bmx
