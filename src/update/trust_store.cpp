#include "update/trust_store.h"

namespace bmx {
namespace update {

namespace {

// Keep the terminating NUL in public_key_pem_size: Mbed TLS requires it for
// PEM input. The private halves remain outside Git. Test artifacts use a
// separate signing domain so a public TEST/UNSAFE release cannot be copied to
// kdre/bmx and authenticated by a production binary.
#if defined(BMX_UPDATE_TEST_CHANNEL)
static const uint8_t kTestReleasePublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEShbFDnu2fo8xFBGV/y4o/Lsa9Bqy\n"
    "1dGRPPRLZH5gDGyfQscChxxNnO76JIEMhIZ7trl1yttB1Hku80K3Aos4Zw==\n"
    "-----END PUBLIC KEY-----\n";

static const TrustedReleaseKey kReleaseKeys[] = {
    {"p256-5494cdd5cfa1c479", kTestReleasePublicKey,
     sizeof(kTestReleasePublicKey), false}
};
#else
static const uint8_t kProductionReleasePublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE4Ctuk+qBVmzhU9YRDqpgkSK08OoF\n"
    "sUbk+UzQE6iVkhLwXAAERjo6RhIzxjbZ+dU0OQ11dgk6jx/bNvGxwy6LCw==\n"
    "-----END PUBLIC KEY-----\n";

static const TrustedReleaseKey kReleaseKeys[] = {
    {"p256-9d2fe3155e45a903", kProductionReleasePublicKey,
     sizeof(kProductionReleasePublicKey), false}
};
#endif

}  // namespace

const TrustedReleaseKey *ReleaseTrustStore(size_t *key_count) {
    if (key_count != 0) {
        *key_count = sizeof(kReleaseKeys) / sizeof(kReleaseKeys[0]);
    }
    return kReleaseKeys;
}

}  // namespace update
}  // namespace bmx
