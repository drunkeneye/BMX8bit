#ifndef BMX_UPDATE_CIRCLE_SECURE_STREAM_H
#define BMX_UPDATE_CIRCLE_SECURE_STREAM_H

#include "update/https_stream.h"

class CNetSubSystem;

namespace bmx {
namespace update {

static const uint32_t kCircleTlsIdleTimeoutMilliseconds = 15000U;
static const uint32_t kCircleTlsHandshakeTimeoutMilliseconds = 30000U;
static const uint32_t kCircleHttpsTotalTimeoutMilliseconds = 15U * 60U * 1000U;

// Uses Circle's continuous 64-bit monotonic microsecond counter. Keeping the
// duration instead of an absolute end avoids overflow near UINT64_MAX.
struct MonotonicDeadline {
    uint64_t start_microseconds;
    uint64_t duration_microseconds;
};

bool InitializeMonotonicDeadline(uint64_t now_microseconds,
                                 uint32_t timeout_milliseconds,
                                 MonotonicDeadline *deadline);
bool RemainingDeadlineMilliseconds(const MonotonicDeadline &deadline,
                                   uint64_t now_microseconds,
                                   uint32_t per_operation_cap_milliseconds,
                                   uint32_t *remaining_milliseconds);

// Establishes a UTC lower bound from locally installed release metadata.
// It never moves a valid clock backwards. The input must come from a trusted
// manual bootstrap or a previously signature-verified release manifest.
bool EnsureCertificateClockAtLeast(uint64_t trusted_epoch,
                                   char *error, size_t error_size);

class CircleSecureStreamFactory : public SecureStreamFactory {
 public:
    CircleSecureStreamFactory(CNetSubSystem *network,
                              uint64_t minimum_trusted_epoch);
    ~CircleSecureStreamFactory();

    SecureByteStream *ConnectVerified(const char *host, uint16_t port,
                                      char *error,
                                      size_t error_size) override;
    void Destroy(SecureByteStream *stream) override;

 private:
    CircleSecureStreamFactory(const CircleSecureStreamFactory &);
    CircleSecureStreamFactory &operator=(const CircleSecureStreamFactory &);

    CNetSubSystem *network_;
    uint64_t minimum_trusted_epoch_;
    void *tls_support_;
    SecureByteStream *active_stream_;
};

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_CIRCLE_SECURE_STREAM_H
