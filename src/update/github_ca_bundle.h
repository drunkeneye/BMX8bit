#ifndef BMX_UPDATE_GITHUB_CA_BUNDLE_H
#define BMX_UPDATE_GITHUB_CA_BUNDLE_H

#include "update/update_types.h"

namespace bmx {
namespace update {

struct CertificatePem {
    const uint8_t *data;
    size_t size;
    const char *sha256_fingerprint;
};

// Deliberately narrow roots needed by the GitHub/Sectigo and GitHub release
// asset/Let's Encrypt chains observed and documented for this updater.
const CertificatePem *GitHubCertificateAuthorities(size_t *count);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_GITHUB_CA_BUNDLE_H
