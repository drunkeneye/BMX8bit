#include "update/url_policy.h"

#include "update/github_repository_policy.h"

#include <string.h>

namespace bmx {
namespace update {

namespace {

bool IsAsciiHostCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '-';
}

bool IsForbiddenRequestCharacter(unsigned char c) {
    return c <= 0x20U || c == 0x7fU || c == '\\';
}

bool StringEquals(const char *left, const char *right) {
    return strcmp(left, right) == 0;
}

bool StartsWith(const char *value, const char *prefix) {
    const size_t prefix_length = strlen(prefix);
    return strncmp(value, prefix, prefix_length) == 0;
}

bool IsGithubReleasePath(const char *path) {
    if (!StartsWith(path, kGitHubReleaseDownloadPathPrefix)) {
        return false;
    }
    const char *tag = path + strlen(kGitHubReleaseDownloadPathPrefix);
    const char *slash = strchr(tag, '/');
    if (slash == 0 || slash == tag || slash[1] == '\0') {
        return false;
    }
    // Exactly one path component for the tag and one for the asset. GitHub
    // release names are emitted by our tools and never require encoded '/'.
    return strchr(slash + 1, '/') == 0;
}

bool IsRedirectPath(const char *path) {
    return path[0] == '/' && path[1] != '\0';
}

bool IsPositiveDecimalSuffix(const char *path, const char *prefix) {
    if (!StartsWith(path, prefix)) return false;
    const char *value = path + strlen(prefix);
    if (*value < '1' || *value > '9') return false;
    for (++value; *value != '\0'; ++value) {
        if (*value < '0' || *value > '9') return false;
    }
    return true;
}

}  // namespace

bool IsAuthorizedRedirectHost(const char *host) {
    if (host == 0) {
        return false;
    }
    return StringEquals(host, "github.com") ||
           StringEquals(host, "release-assets.githubusercontent.com") ||
           StringEquals(host, "objects.githubusercontent.com") ||
           StringEquals(host, "github-releases.githubusercontent.com");
}

UrlPolicyStatus ParseAndAuthorizeUpdateUrl(const char *url,
                                           UpdateUrlPurpose purpose,
                                           ParsedUpdateUrl *parsed) {
    if (url == 0 || parsed == 0) {
        return UrlPolicyStatus::InvalidArgument;
    }
    const size_t length = strlen(url);
    if (length == 0U || length >= kMaximumUpdateUrlBytes) {
        return UrlPolicyStatus::TooLong;
    }
    static const char kScheme[] = "https://";
    if (strncmp(url, kScheme, sizeof(kScheme) - 1U) != 0) {
        return UrlPolicyStatus::HttpsRequired;
    }

    const char *authority = url + sizeof(kScheme) - 1U;
    const char *path = strchr(authority, '/');
    const char *query_without_path = strchr(authority, '?');
    const char *fragment = strchr(authority, '#');
    if (fragment != 0) {
        return UrlPolicyStatus::FragmentForbidden;
    }
    if (query_without_path != 0 && (path == 0 || query_without_path < path)) {
        return UrlPolicyStatus::InvalidPath;
    }
    if (path == 0 || path == authority) {
        return UrlPolicyStatus::InvalidPath;
    }
    const char *at = static_cast<const char *>(
        memchr(authority, '@', static_cast<size_t>(path - authority)));
    if (at != 0) {
        return UrlPolicyStatus::UserInfoForbidden;
    }

    const char *port_separator = static_cast<const char *>(
        memchr(authority, ':', static_cast<size_t>(path - authority)));
    const char *host_end = port_separator != 0 ? port_separator : path;
    const size_t host_length = static_cast<size_t>(host_end - authority);
    if (host_length == 0U || host_length >= sizeof(parsed->host)) {
        return UrlPolicyStatus::InvalidHost;
    }
    for (size_t i = 0; i < host_length; ++i) {
        if (!IsAsciiHostCharacter(authority[i])) {
            return UrlPolicyStatus::InvalidHost;
        }
    }
    memcpy(parsed->host, authority, host_length);
    parsed->host[host_length] = '\0';

    parsed->port = 443U;
    if (port_separator != 0) {
        const size_t port_length = static_cast<size_t>(path - port_separator - 1);
        if (port_length != 3U || memcmp(port_separator + 1, "443", 3U) != 0) {
            return UrlPolicyStatus::InvalidPort;
        }
    }

    const size_t path_length = strlen(path);
    if (path_length == 0U || path_length >= sizeof(parsed->path_and_query)) {
        return UrlPolicyStatus::InvalidPath;
    }
    for (size_t i = 0; i < path_length; ++i) {
        if (IsForbiddenRequestCharacter(static_cast<unsigned char>(path[i]))) {
            return UrlPolicyStatus::InvalidPath;
        }
    }
    memcpy(parsed->path_and_query, path, path_length + 1U);

    const char *query = strchr(parsed->path_and_query, '?');
    if (purpose != UpdateUrlPurpose::Redirect && query != 0) {
        return UrlPolicyStatus::QueryForbidden;
    }

    if (purpose == UpdateUrlPurpose::Discovery) {
        if (!StringEquals(parsed->host, "api.github.com")) {
            return UrlPolicyStatus::HostNotAllowed;
        }
        if (!StringEquals(parsed->path_and_query, kGitHubLatestReleasePath)) {
            return UrlPolicyStatus::PathNotAllowed;
        }
        return UrlPolicyStatus::Ok;
    }

    if (purpose == UpdateUrlPurpose::ReleaseAsset) {
        if (!StringEquals(parsed->host, "github.com")) {
            return UrlPolicyStatus::HostNotAllowed;
        }
        if (!IsGithubReleasePath(parsed->path_and_query)) {
            return UrlPolicyStatus::PathNotAllowed;
        }
        return UrlPolicyStatus::Ok;
    }

    if (purpose == UpdateUrlPurpose::PreparedDraftDiscovery) {
        if (!StringEquals(parsed->host, "api.github.com")) {
            return UrlPolicyStatus::HostNotAllowed;
        }
        return IsPositiveDecimalSuffix(parsed->path_and_query,
                                       kGitHubReleaseApiPathPrefix)
                   ? UrlPolicyStatus::Ok
                   : UrlPolicyStatus::PathNotAllowed;
    }

    if (purpose == UpdateUrlPurpose::AuthenticatedReleaseAsset) {
        if (!StringEquals(parsed->host, "api.github.com")) {
            return UrlPolicyStatus::HostNotAllowed;
        }
        return IsPositiveDecimalSuffix(parsed->path_and_query,
                                       kGitHubReleaseAssetApiPathPrefix)
                   ? UrlPolicyStatus::Ok
                   : UrlPolicyStatus::PathNotAllowed;
    }

    if (purpose == UpdateUrlPurpose::GitHubDeviceAuthorization) {
        if (!StringEquals(parsed->host, "github.com")) {
            return UrlPolicyStatus::HostNotAllowed;
        }
        return StringEquals(parsed->path_and_query, "/login/device/code") ||
                       StringEquals(parsed->path_and_query,
                                    "/login/oauth/access_token")
                   ? UrlPolicyStatus::Ok
                   : UrlPolicyStatus::PathNotAllowed;
    }

    if (!IsAuthorizedRedirectHost(parsed->host)) {
        return UrlPolicyStatus::HostNotAllowed;
    }
    if (!IsRedirectPath(parsed->path_and_query)) {
        return UrlPolicyStatus::PathNotAllowed;
    }
    return UrlPolicyStatus::Ok;
}

const char *UrlPolicyStatusString(UrlPolicyStatus status) {
    switch (status) {
        case UrlPolicyStatus::Ok: return "ok";
        case UrlPolicyStatus::InvalidArgument: return "invalid argument";
        case UrlPolicyStatus::TooLong: return "URL too long";
        case UrlPolicyStatus::HttpsRequired: return "HTTPS required";
        case UrlPolicyStatus::UserInfoForbidden: return "URL user-info forbidden";
        case UrlPolicyStatus::InvalidHost: return "invalid URL host";
        case UrlPolicyStatus::InvalidPort: return "only HTTPS port 443 allowed";
        case UrlPolicyStatus::InvalidPath: return "invalid URL path";
        case UrlPolicyStatus::QueryForbidden: return "URL query forbidden";
        case UrlPolicyStatus::FragmentForbidden: return "URL fragment forbidden";
        case UrlPolicyStatus::HostNotAllowed: return "URL host not allowed";
        case UrlPolicyStatus::PathNotAllowed: return "URL path not allowed";
    }
    return "unknown URL policy error";
}

}  // namespace update
}  // namespace bmx
