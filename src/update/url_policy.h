#ifndef BMX_UPDATE_URL_POLICY_H
#define BMX_UPDATE_URL_POLICY_H

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace update {

static const size_t kMaximumUpdateUrlBytes = 2048U;
static const size_t kMaximumUpdateHostBytes = 96U;
static const size_t kMaximumUpdatePathBytes = 1856U;

enum class UpdateUrlPurpose : uint8_t {
    Discovery = 0,
    ReleaseAsset = 1,
    Redirect = 2,
    PreparedDraftDiscovery = 3,
    AuthenticatedReleaseAsset = 4,
    GitHubDeviceAuthorization = 5
};

enum class UrlPolicyStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    TooLong,
    HttpsRequired,
    UserInfoForbidden,
    InvalidHost,
    InvalidPort,
    InvalidPath,
    QueryForbidden,
    FragmentForbidden,
    HostNotAllowed,
    PathNotAllowed
};

struct ParsedUpdateUrl {
    char host[kMaximumUpdateHostBytes];
    char path_and_query[kMaximumUpdatePathBytes];
    uint16_t port;
};

// Accepts only HTTPS, port 443 and the minimum GitHub-owned host/path set
// needed by the updater. It never resolves relative URLs.
UrlPolicyStatus ParseAndAuthorizeUpdateUrl(const char *url,
                                           UpdateUrlPurpose purpose,
                                           ParsedUpdateUrl *parsed);

bool IsAuthorizedRedirectHost(const char *host);
const char *UrlPolicyStatusString(UrlPolicyStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_URL_POLICY_H
