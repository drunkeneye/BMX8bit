#include "update/signature_verifier.h"

#include <string.h>

#if defined(RASPI_COMPILE) || defined(BMX_UPDATE_USE_MBEDTLS)
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#endif

namespace bmx {
namespace update {

ManifestTrustResult VerifyManifestTrust(
    ByteView manifest_bytes, const ReleaseManifest &manifest,
    ByteView signature_envelope, const TrustedReleaseKey *trusted_keys,
    size_t trusted_key_count, VerifyP256SignatureFunction verify_function,
    void *verify_context) {
    ManifestTrustResult result;
    memset(&result, 0, sizeof(result));
    result.status = ManifestTrustStatus::InvalidArgument;
    result.envelope_status = SignatureEnvelopeStatus::InvalidArgument;
    if (manifest_bytes.data == 0 || manifest_bytes.size == 0U ||
        signature_envelope.data == 0 || trusted_keys == 0 ||
        trusted_key_count == 0U || verify_function == 0) {
        return result;
    }

    ReleaseSignature signatures[kMaximumReleaseSignatures];
    size_t signature_count = 0U;
    result.envelope_status = ParseSignatureEnvelope(
        signature_envelope, signatures, kMaximumReleaseSignatures,
        &signature_count);
    if (result.envelope_status != SignatureEnvelopeStatus::Ok) {
        result.status = ManifestTrustStatus::EnvelopeInvalid;
        return result;
    }

    bool saw_trusted_key = false;
    bool saw_undeclared = false;
    for (size_t signature_index = 0U; signature_index < signature_count;
         ++signature_index) {
        const ReleaseSignature &signature = signatures[signature_index];
        if (!ManifestListsSigningKey(manifest, signature.key_id)) {
            saw_undeclared = true;
            continue;
        }
        for (size_t key_index = 0U; key_index < trusted_key_count; ++key_index) {
            const TrustedReleaseKey &key = trusted_keys[key_index];
            if (key.key_id == 0 || strcmp(key.key_id, signature.key_id) != 0 ||
                key.revoked) {
                continue;
            }
            saw_trusted_key = true;
            if (key.public_key_pem == 0 || key.public_key_pem_size == 0U) {
                continue;
            }
            if (verify_function(ByteView(key.public_key_pem,
                                         key.public_key_pem_size),
                                manifest_bytes,
                                ByteView(signature.der, signature.der_size),
                                verify_context)) {
                result.status = ManifestTrustStatus::Trusted;
                memcpy(result.verified_key_id, signature.key_id,
                       strlen(signature.key_id) + 1U);
                return result;
            }
        }
    }
    if (saw_undeclared && !saw_trusted_key) {
        result.status = ManifestTrustStatus::UndeclaredSignature;
    } else if (!saw_trusted_key) {
        result.status = ManifestTrustStatus::NoTrustedKey;
    } else {
        result.status = ManifestTrustStatus::SignatureMismatch;
    }
    return result;
}

bool VerifyP256SignatureMbedTls(ByteView public_key_pem, ByteView message,
                               ByteView signature_der, void *context) {
    (void) context;
#if defined(RASPI_COMPILE) || defined(BMX_UPDATE_USE_MBEDTLS)
    if (public_key_pem.data == 0 || public_key_pem.size == 0U ||
        message.data == 0 || signature_der.data == 0 ||
        !IsValidP256EcdsaDer(signature_der)) {
        return false;
    }
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    int status = mbedtls_pk_parse_public_key(
        &key, public_key_pem.data, public_key_pem.size);
    if (status != 0 || !mbedtls_pk_can_do(&key, MBEDTLS_PK_ECDSA)) {
        mbedtls_pk_free(&key);
        return false;
    }
    const mbedtls_ecp_keypair *ec = mbedtls_pk_ec(key);
    if (ec == 0 ||
        mbedtls_ecp_keypair_get_group_id(ec) != MBEDTLS_ECP_DP_SECP256R1) {
        mbedtls_pk_free(&key);
        return false;
    }
    uint8_t digest[kSha256DigestBytes];
    status = mbedtls_sha256(message.data, message.size, digest, 0);
    if (status == 0) {
        status = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest,
                                   sizeof(digest), signature_der.data,
                                   signature_der.size);
    }
    mbedtls_pk_free(&key);
    memset(digest, 0, sizeof(digest));
    return status == 0;
#else
    (void) public_key_pem;
    (void) message;
    (void) signature_der;
    return false;
#endif
}

const char *ManifestTrustStatusString(ManifestTrustStatus status) {
    switch (status) {
        case ManifestTrustStatus::Trusted: return "trusted";
        case ManifestTrustStatus::InvalidArgument: return "invalid argument";
        case ManifestTrustStatus::EnvelopeInvalid: return "signature envelope invalid";
        case ManifestTrustStatus::UndeclaredSignature: return "signature key not declared by manifest";
        case ManifestTrustStatus::NoTrustedKey: return "no trusted release key";
        case ManifestTrustStatus::SignatureMismatch: return "manifest signature mismatch";
    }
    return "unknown manifest trust error";
}

}  // namespace update
}  // namespace bmx
