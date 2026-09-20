#ifndef BMX_UPDATE_TRUST_STORE_H
#define BMX_UPDATE_TRUST_STORE_H

#include "update/signature_verifier.h"

namespace bmx {
namespace update {

// Returns the immutable release-signing trust anchors compiled into BMX.
// Revocations and key rotation are shipped as normal BMX binaries; an
// untrusted manifest can never add a key to this list.
const TrustedReleaseKey *ReleaseTrustStore(size_t *key_count);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_TRUST_STORE_H
