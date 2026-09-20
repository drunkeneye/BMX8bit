#ifndef BMX_UPDATE_SIGNATURE_VERIFIER_H
#define BMX_UPDATE_SIGNATURE_VERIFIER_H

#include "update/release_manifest.h"
#include "update/signature_envelope.h"

namespace bmx {
namespace update {

struct TrustedReleaseKey {
    const char *key_id;
    const uint8_t *public_key_pem;
    size_t public_key_pem_size;
    bool revoked;
};

typedef bool (*VerifyP256SignatureFunction)(ByteView public_key_pem,
                                            ByteView message,
                                            ByteView signature_der,
                                            void *context);

enum class ManifestTrustStatus : uint8_t {
    Trusted = 0,
    InvalidArgument,
    EnvelopeInvalid,
    UndeclaredSignature,
    NoTrustedKey,
    SignatureMismatch
};

struct ManifestTrustResult {
    ManifestTrustStatus status;
    SignatureEnvelopeStatus envelope_status;
    char verified_key_id[kMaximumSigningKeyIdBytes + 1U];
};

ManifestTrustResult VerifyManifestTrust(
    ByteView manifest_bytes, const ReleaseManifest &manifest,
    ByteView signature_envelope, const TrustedReleaseKey *trusted_keys,
    size_t trusted_key_count, VerifyP256SignatureFunction verify_function,
    void *verify_context);

// Mbed TLS-backed implementation used by the Circle target. It is kept behind
// a simple callback so all trust/envelope logic is host-testable without the
// target crypto library.
bool VerifyP256SignatureMbedTls(ByteView public_key_pem, ByteView message,
                               ByteView signature_der, void *context);

const char *ManifestTrustStatusString(ManifestTrustStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_SIGNATURE_VERIFIER_H
