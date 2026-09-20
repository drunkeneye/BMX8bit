#ifndef BMX_UPDATE_GITHUB_RELEASE_PARSER_H
#define BMX_UPDATE_GITHUB_RELEASE_PARSER_H

#include "update/json_parser.h"
#include "update/update_types.h"
#include "update/url_policy.h"

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace update {

static const size_t kMaximumGitHubReleaseResponseBytes = 128U * 1024U;
static const size_t kMaximumGitHubReleaseAssets = 64U;
static const size_t kMaximumGitHubReleaseTagBytes = 128U;
static const size_t kMaximumGitHubAssetNameBytes = 160U;

struct GitHubReleaseAsset {
    uint64_t id;
    char name[kMaximumGitHubAssetNameBytes + 1U];
    uint64_t size;
    char browser_download_url[kMaximumUpdateUrlBytes];
    char api_download_url[kMaximumUpdateUrlBytes];
};

struct GitHubLatestRelease {
    uint64_t id;
    char tag_name[kMaximumGitHubReleaseTagBytes + 1U];
    bool draft;
    bool prerelease;
    GitHubReleaseAsset *assets;
    size_t asset_count;
};

enum class GitHubReleaseKind : uint8_t {
    PublishedStable = 0,
    PreparedDraft = 1
};

struct GitHubReleaseParseStorage {
    JsonToken *tokens;
    size_t token_capacity;
    GitHubReleaseAsset *assets;
    size_t asset_capacity;
};

enum class GitHubReleaseParseStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    ResponseTooLarge,
    StorageTooSmall,
    JsonInvalid,
    WrongRootType,
    MissingField,
    WrongType,
    InvalidValue,
    TooManyAssets,
    DuplicateAssetName,
    UrlRejected,
    UrlBindingMismatch,
    DraftForbidden,
    PrereleaseForbidden
};

// Parses the untrusted response body from GitHub's /releases/latest endpoint.
// The function allocates nothing: both token and asset arrays belong to the
// caller and must remain alive while |release| is used.
GitHubReleaseParseStatus ParseGitHubLatestRelease(
    ByteView encoded,
    const GitHubReleaseParseStorage &storage,
    GitHubLatestRelease *release,
    JsonParseResult *json_result);

GitHubReleaseParseStatus ParseGitHubRelease(
    ByteView encoded,
    const GitHubReleaseParseStorage &storage,
    GitHubReleaseKind kind,
    GitHubLatestRelease *release,
    JsonParseResult *json_result);

struct RequiredGitHubReleaseAssets {
    const GitHubReleaseAsset *manifest;
    const GitHubReleaseAsset *signature;
    const GitHubReleaseAsset *board_zip;
};

struct GitHubReleaseMetadataAssets {
    const GitHubReleaseAsset *manifest;
    const GitHubReleaseAsset *signature;
};

enum class RequiredGitHubAssetsStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    IneligibleRelease,
    InvalidSignedTag,
    InvalidBoardZipName,
    SignedTagMismatch,
    TooManyAssets,
    DuplicateAssetName,
    UrlRejected,
    UrlBindingMismatch,
    MissingManifest,
    MissingSignature,
    MissingBoardZip
};

// Locates the two fixed-name metadata assets needed before the manifest can
// be authenticated. Their URLs remain bound to the already validated GitHub
// release tag. The board ZIP is deliberately selected only after signature
// verification via FindRequiredAssets().
RequiredGitHubAssetsStatus FindReleaseMetadataAssets(
    const GitHubLatestRelease &release,
    GitHubReleaseMetadataAssets *metadata);

RequiredGitHubAssetsStatus FindReleaseMetadataAssetsForKind(
    const GitHubLatestRelease &release,
    GitHubReleaseKind kind,
    GitHubReleaseMetadataAssets *metadata);

// Must be called only after BMX-RELEASE.json has been authenticated. The tag
// and board ZIP name are therefore signed inputs. The function binds those
// inputs to both the discovery response and the exact GitHub download paths.
RequiredGitHubAssetsStatus FindRequiredAssets(
    const GitHubLatestRelease &release,
    const char *signed_manifest_tag,
    const char *signed_board_zip_name,
    RequiredGitHubReleaseAssets *required);

RequiredGitHubAssetsStatus FindRequiredAssetsForKind(
    const GitHubLatestRelease &release,
    GitHubReleaseKind kind,
    const char *signed_manifest_tag,
    const char *signed_board_zip_name,
    RequiredGitHubReleaseAssets *required);

const char *GitHubAssetDownloadUrl(const GitHubReleaseAsset &asset,
                                   GitHubReleaseKind kind);

const char *GitHubReleaseParseStatusString(GitHubReleaseParseStatus status);
const char *RequiredGitHubAssetsStatusString(RequiredGitHubAssetsStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_GITHUB_RELEASE_PARSER_H
