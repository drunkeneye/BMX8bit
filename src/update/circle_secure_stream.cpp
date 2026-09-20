#include "update/circle_secure_stream.h"

#ifndef BMX_UPDATE_DEADLINE_POLICY_ONLY
#include "update/github_ca_bundle.h"

#include <circle-mbedtls/tlssimpleclientsocket.h>
#include <circle-mbedtls/tlssimplesupport.h>
#include <circle/net/in.h>
#include <circle/net/netsubsystem.h>
#include <circle/timer.h>
#endif

#include <limits.h>
#ifndef BMX_UPDATE_DEADLINE_POLICY_ONLY
#include <stdio.h>
#include <string.h>
#endif

namespace bmx {
namespace update {

bool InitializeMonotonicDeadline(uint64_t now_microseconds,
                                 uint32_t timeout_milliseconds,
                                 MonotonicDeadline *deadline) {
    if (deadline == 0 || timeout_milliseconds == 0U) return false;
    deadline->start_microseconds = now_microseconds;
    deadline->duration_microseconds =
        static_cast<uint64_t>(timeout_milliseconds) * 1000U;
    return deadline->duration_microseconds != 0U;
}

bool RemainingDeadlineMilliseconds(const MonotonicDeadline &deadline,
                                   uint64_t now_microseconds,
                                   uint32_t per_operation_cap_milliseconds,
                                   uint32_t *remaining_milliseconds) {
    if (remaining_milliseconds == 0 ||
        per_operation_cap_milliseconds == 0U ||
        deadline.duration_microseconds == 0U ||
        now_microseconds < deadline.start_microseconds) {
        return false;
    }
    const uint64_t elapsed = now_microseconds - deadline.start_microseconds;
    if (elapsed >= deadline.duration_microseconds) return false;
    const uint64_t remaining_microseconds =
        deadline.duration_microseconds - elapsed;
    uint64_t remaining = remaining_microseconds / 1000U;
    if (remaining_microseconds % 1000U != 0U) ++remaining;
    if (remaining > per_operation_cap_milliseconds) {
        remaining = per_operation_cap_milliseconds;
    }
    if (remaining == 0U || remaining > UINT_MAX) return false;
    *remaining_milliseconds = static_cast<uint32_t>(remaining);
    return true;
}

#ifndef BMX_UPDATE_DEADLINE_POLICY_ONLY

namespace {

using CircleMbedTLS::CTLSSimpleClientSocket;
using CircleMbedTLS::CTLSSimpleSupport;

void SetError(char *error, size_t error_size, const char *message,
              int code = 0) {
    if (error == 0 || error_size == 0U) return;
    if (code == 0) {
        snprintf(error, error_size, "%s", message);
    } else {
        snprintf(error, error_size, "%s (%d)", message, code);
    }
    error[error_size - 1U] = '\0';
}

class CircleSecureByteStream : public SecureByteStream {
 public:
    CircleSecureByteStream(CTLSSimpleClientSocket *socket,
                           const MonotonicDeadline &deadline)
        : socket_(socket), deadline_(deadline), closed_(false) {}

    ~CircleSecureByteStream() override {
        delete socket_;
        socket_ = 0;
    }

    int Send(const uint8_t *data, size_t size) override {
        if (closed_ || socket_ == 0 || data == 0 || size == 0U) return -1;
        uint32_t remaining = 0U;
        if (!RemainingDeadlineMilliseconds(
                deadline_, CTimer::GetClockTicks64(),
                kCircleTlsIdleTimeoutMilliseconds, &remaining)) {
            closed_ = true;
            return -1;
        }
        const unsigned amount = size > UINT_MAX
                                    ? UINT_MAX
                                    : static_cast<unsigned>(size);
        return socket_->Send(data, amount, 0);
    }

    int Receive(uint8_t *data, size_t capacity) override {
        if (closed_ || socket_ == 0 || data == 0 || capacity == 0U) return -1;
        uint32_t remaining = 0U;
        if (!RemainingDeadlineMilliseconds(
                deadline_, CTimer::GetClockTicks64(),
                kCircleTlsIdleTimeoutMilliseconds, &remaining)) {
            closed_ = true;
            return -1;
        }
        const unsigned amount = capacity > UINT_MAX
                                    ? UINT_MAX
                                    : static_cast<unsigned>(capacity);
        return socket_->ReceiveWithTimeout(data, amount, 0, remaining);
    }

    void Close() override { closed_ = true; }

 private:
    CTLSSimpleClientSocket *socket_;
    MonotonicDeadline deadline_;
    bool closed_;
};

}  // namespace

bool EnsureCertificateClockAtLeast(uint64_t trusted_epoch,
                                   char *error, size_t error_size) {
    if (trusted_epoch == 0U || trusted_epoch > UINT_MAX || CTimer::Get() == 0) {
        SetError(error, error_size, "trusted certificate time is unavailable");
        return false;
    }
    const unsigned current = CTimer::Get()->GetUniversalTime();
    if (current >= static_cast<unsigned>(trusted_epoch)) return true;
    if (!CTimer::Get()->SetTime(static_cast<unsigned>(trusted_epoch), FALSE)) {
        SetError(error, error_size, "cannot establish trusted certificate time");
        return false;
    }
    return CTimer::Get()->GetUniversalTime() >= trusted_epoch;
}

CircleSecureStreamFactory::CircleSecureStreamFactory(
    CNetSubSystem *network, uint64_t minimum_trusted_epoch)
    : network_(network), minimum_trusted_epoch_(minimum_trusted_epoch),
      tls_support_(0), active_stream_(0) {
    if (network_ != 0) {
        tls_support_ = new CTLSSimpleSupport(network_);
    }
}

CircleSecureStreamFactory::~CircleSecureStreamFactory() {
    Destroy(active_stream_);
    delete static_cast<CTLSSimpleSupport *>(tls_support_);
    tls_support_ = 0;
    network_ = 0;
}

SecureByteStream *CircleSecureStreamFactory::ConnectVerified(
    const char *host, uint16_t port, char *error, size_t error_size) {
    if (error != 0 && error_size != 0U) error[0] = '\0';
    if (active_stream_ != 0 || network_ == 0 || !network_->IsRunning() ||
        tls_support_ == 0 || host == 0 || host[0] == '\0' || port != 443U) {
        SetError(error, error_size, "TLS connection arguments invalid");
        return 0;
    }
    const unsigned now = CTimer::Get() != 0
                             ? CTimer::Get()->GetUniversalTime()
                             : 0U;
    if (minimum_trusted_epoch_ == 0U || now < minimum_trusted_epoch_) {
        SetError(error, error_size, "trusted certificate time not established");
        return 0;
    }
    MonotonicDeadline total_deadline;
    if (!InitializeMonotonicDeadline(
            CTimer::GetClockTicks64(),
            kCircleHttpsTotalTimeoutMilliseconds, &total_deadline)) {
        SetError(error, error_size, "cannot initialize HTTPS deadline");
        return 0;
    }

    CTLSSimpleClientSocket *socket = new CTLSSimpleClientSocket(
        static_cast<CTLSSimpleSupport *>(tls_support_), IPPROTO_TCP);
    if (socket == 0) {
        SetError(error, error_size, "cannot allocate TLS socket");
        return 0;
    }
    int status = socket->SetTimeouts(
        kCircleTlsIdleTimeoutMilliseconds,
        kCircleTlsHandshakeTimeoutMilliseconds);
    if (status != 0) {
        SetError(error, error_size, "cannot configure TLS timeouts", status);
        delete socket;
        return 0;
    }
    size_t ca_count = 0U;
    const CertificatePem *authorities = GitHubCertificateAuthorities(&ca_count);
    for (size_t i = 0U; i < ca_count; ++i) {
        const int status = socket->AddCertificate(authorities[i].data,
                                                  authorities[i].size);
        if (status != 0) {
            SetError(error, error_size, "cannot load pinned CA", status);
            delete socket;
            return 0;
        }
    }
    status = socket->Setup(host, "BMX updater TLS v1");
    if (status == 0) status = socket->Connect(host, "443");
    if (status != 0) {
        SetError(error, error_size, "verified TLS handshake failed", status);
        delete socket;
        return 0;
    }
    uint32_t remaining = 0U;
    if (!RemainingDeadlineMilliseconds(
            total_deadline, CTimer::GetClockTicks64(),
            kCircleTlsIdleTimeoutMilliseconds, &remaining)) {
        delete socket;
        SetError(error, error_size, "verified TLS connection deadline exceeded");
        return 0;
    }
    active_stream_ = new CircleSecureByteStream(socket, total_deadline);
    if (active_stream_ == 0) {
        delete socket;
        SetError(error, error_size, "cannot allocate TLS stream");
        return 0;
    }
    return active_stream_;
}

void CircleSecureStreamFactory::Destroy(SecureByteStream *stream) {
    if (stream == 0) return;
    if (stream != active_stream_) return;
    delete active_stream_;
    active_stream_ = 0;
}

#endif  // BMX_UPDATE_DEADLINE_POLICY_ONLY

}  // namespace update
}  // namespace bmx
