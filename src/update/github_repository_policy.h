#ifndef BMX_UPDATE_GITHUB_REPOSITORY_POLICY_H
#define BMX_UPDATE_GITHUB_REPOSITORY_POLICY_H

#include "update/update_hardware_test_mode.h"

#include <stddef.h>

// The updater repository is a build-time trust boundary.  Production builds
// have no override mechanism and are always pinned to kdre/bmx.  A separate
// repository can be compiled in only for an explicitly marked debug build.
#if defined(BMX_UPDATE_TEST_CHANNEL) && !defined(BMC64_DEBUG_PROFILE)
#error "BMX_UPDATE_TEST_CHANNEL requires BMC64_DEBUG_PROFILE"
#endif

#if defined(BMX_UPDATE_TEST_CHANNEL)
#if !defined(BMX_UPDATE_TEST_REPOSITORY_OWNER)
#error "BMX_UPDATE_TEST_CHANNEL requires BMX_UPDATE_TEST_REPOSITORY_OWNER"
#endif
#if !defined(BMX_UPDATE_TEST_REPOSITORY_NAME)
#error "BMX_UPDATE_TEST_CHANNEL requires BMX_UPDATE_TEST_REPOSITORY_NAME"
#endif
#else
#if defined(BMX_UPDATE_TEST_REPOSITORY_OWNER) || \
    defined(BMX_UPDATE_TEST_REPOSITORY_NAME)
#error "test repository macros require BMX_UPDATE_TEST_CHANNEL"
#endif
#endif

namespace bmx {
namespace update {

static const size_t kMaximumGitHubRepositoryOwnerBytes = 64U;
static const size_t kMaximumGitHubRepositoryNameBytes = 100U;

namespace repository_policy_detail {

constexpr bool IsSafeRepositoryCharacter(char value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '.' || value == '_' ||
           value == '-';
}

constexpr char AsciiLower(char value) {
    return value >= 'A' && value <= 'Z'
               ? static_cast<char>(value - 'A' + 'a')
               : value;
}

constexpr bool EqualsAsciiCaseInsensitiveRange(const char *left,
                                                const char *right,
                                                size_t index,
                                                size_t end) {
    return index == end
               ? true
               : (AsciiLower(left[index]) == AsciiLower(right[index]) &&
                  EqualsAsciiCaseInsensitiveRange(
                      left, right, index + 1U, end));
}

template <size_t LeftN, size_t RightN>
constexpr bool EqualsAsciiCaseInsensitive(const char (&left)[LeftN],
                                          const char (&right)[RightN]) {
    return LeftN == RightN &&
           EqualsAsciiCaseInsensitiveRange(left, right, 0U, LeftN - 1U);
}

constexpr bool IsSafeRepositoryLiteralRange(const char *value,
                                            size_t index,
                                            size_t end) {
    return index == end
               ? true
               : (IsSafeRepositoryCharacter(value[index]) &&
                  IsSafeRepositoryLiteralRange(value, index + 1U, end));
}

template <size_t N>
constexpr bool IsSafeRepositoryLiteral(const char (&value)[N],
                                       size_t maximum_bytes) {
    return N > 1U && N <= maximum_bytes + 1U &&
           !(N == 2U && value[0] == '.') &&
           !(N == 3U && value[0] == '.' && value[1] == '.') &&
           IsSafeRepositoryLiteralRange(value, 0U, N - 1U);
}

}  // namespace repository_policy_detail

#if defined(BMX_UPDATE_TEST_CHANNEL)

static_assert(repository_policy_detail::IsSafeRepositoryLiteral(
                  BMX_UPDATE_TEST_REPOSITORY_OWNER,
                  kMaximumGitHubRepositoryOwnerBytes),
              "unsafe BMX update test repository owner");
static_assert(repository_policy_detail::IsSafeRepositoryLiteral(
                  BMX_UPDATE_TEST_REPOSITORY_NAME,
                  kMaximumGitHubRepositoryNameBytes),
              "unsafe BMX update test repository name");
static_assert(
    !(repository_policy_detail::EqualsAsciiCaseInsensitive(
          BMX_UPDATE_TEST_REPOSITORY_OWNER, "kdre") &&
      repository_policy_detail::EqualsAsciiCaseInsensitive(
          BMX_UPDATE_TEST_REPOSITORY_NAME, "bmx")),
    "BMX update test channel must not target canonical kdre/bmx");

#define BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER \
    BMX_UPDATE_TEST_REPOSITORY_OWNER
#define BMX_UPDATE_CONFIGURED_REPOSITORY_NAME \
    BMX_UPDATE_TEST_REPOSITORY_NAME
#if defined(BMX_UPDATE_HARDWARE_TEST_MODE)
#define BMX_UPDATE_CONFIGURED_CHANNEL_LABEL \
    "TEST/UNSAFE GitHub Releases: " BMX_UPDATE_TEST_REPOSITORY_OWNER "/" \
        BMX_UPDATE_TEST_REPOSITORY_NAME
static const bool kGitHubReleaseHardwareTestMode = true;
#else
#define BMX_UPDATE_CONFIGURED_CHANNEL_LABEL \
    "TEST GitHub Releases: " BMX_UPDATE_TEST_REPOSITORY_OWNER "/" \
        BMX_UPDATE_TEST_REPOSITORY_NAME
static const bool kGitHubReleaseHardwareTestMode = false;
#endif

static const bool kGitHubReleaseChannelIsTest = true;

#else

#define BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "kdre"
#define BMX_UPDATE_CONFIGURED_REPOSITORY_NAME "bmx"
#define BMX_UPDATE_CONFIGURED_CHANNEL_LABEL "GitHub Releases: kdre/bmx"

static const bool kGitHubReleaseChannelIsTest = false;
static const bool kGitHubReleaseHardwareTestMode = false;

#endif

// These values are assembled exclusively from compile-time string literals.
// No configuration file, environment variable, remote response or runtime
// caller can redirect the repository used by the updater.
static const char kGitHubReleaseRepositoryOwner[] =
    BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER;
static const char kGitHubReleaseRepositoryName[] =
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME;
static const char kGitHubReleaseRepositorySlug[] =
    BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "/"
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME;
static const char kGitHubLatestReleaseUrl[] =
    "https://api.github.com/repos/" BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "/"
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME "/releases/latest";
static const char kGitHubLatestReleasePath[] =
    "/repos/" BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "/"
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME "/releases/latest";
static const char kGitHubReleaseDownloadPathPrefix[] =
    "/" BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "/"
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME "/releases/download/";
static const char kGitHubReleaseApiPathPrefix[] =
    "/repos/" BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "/"
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME "/releases/";
static const char kGitHubReleaseAssetApiPathPrefix[] =
    "/repos/" BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER "/"
    BMX_UPDATE_CONFIGURED_REPOSITORY_NAME "/releases/assets/";
static const char kGitHubReleaseChannelLabel[] =
    BMX_UPDATE_CONFIGURED_CHANNEL_LABEL;

struct GitHubReleaseChannelInfo {
    const char *repository_slug;
    const char *display_label;
    bool is_test;
    bool hardware_test_mode;
};

// UI and diagnostics can use this to make a debug test-channel build
// unmistakable.  The returned data remains compile-time fixed.
inline GitHubReleaseChannelInfo ConfiguredGitHubReleaseChannel() {
    GitHubReleaseChannelInfo info = {
        kGitHubReleaseRepositorySlug,
        kGitHubReleaseChannelLabel,
        kGitHubReleaseChannelIsTest,
        kGitHubReleaseHardwareTestMode};
    return info;
}

#undef BMX_UPDATE_CONFIGURED_REPOSITORY_OWNER
#undef BMX_UPDATE_CONFIGURED_REPOSITORY_NAME
#undef BMX_UPDATE_CONFIGURED_CHANNEL_LABEL

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_GITHUB_REPOSITORY_POLICY_H
