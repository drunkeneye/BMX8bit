#ifndef BMX_UPDATE_SIGNATURE_ENVELOPE_H
#define BMX_UPDATE_SIGNATURE_ENVELOPE_H

#include "update/update_types.h"

namespace bmx {
namespace update {

static const size_t kMaximumSignatureEnvelopeBytes = 4096U;
static const size_t kMaximumReleaseSignatures = 4U;
static const size_t kMaximumSigningKeyIdBytes = 64U;
static const size_t kMaximumEcdsaDerBytes = 80U;

struct ReleaseSignature {
    char key_id[kMaximumSigningKeyIdBytes + 1U];
    uint8_t der[kMaximumEcdsaDerBytes];
    size_t der_size;
};

enum class SignatureEnvelopeStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    InvalidSize,
    NonAscii,
    InvalidLineEnding,
    UnsupportedVersion,
    IncompleteRecord,
    SignatureCount,
    InvalidKeyId,
    KeyOrder,
    UnsupportedAlgorithm,
    InvalidBase64,
    InvalidDer
};

SignatureEnvelopeStatus ParseSignatureEnvelope(
    ByteView envelope, ReleaseSignature *signatures, size_t signature_capacity,
    size_t *signature_count);

bool IsValidP256EcdsaDer(ByteView signature);
const char *SignatureEnvelopeStatusString(SignatureEnvelopeStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_SIGNATURE_ENVELOPE_H
