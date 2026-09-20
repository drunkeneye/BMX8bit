#ifndef BMX_UPDATE_HTTPS_STREAM_H
#define BMX_UPDATE_HTTPS_STREAM_H

#include "update/http_response_parser.h"
#include "update/update_types.h"
#include "update/url_policy.h"

namespace bmx {
namespace update {

class UpdateForegroundProgress;

static const unsigned kMaximumHttpsRedirects = 3U;
static const size_t kHttpsRequestBufferBytes = 4096U;
static const size_t kHttpsReceiveBufferBytes = 16384U;

// Mandatory caller-owned scratch for one synchronous HTTPS request chain.
// It is intentionally explicit: these buffers total roughly 59 KiB and must
// live in heap/static storage on Circle's 128 KiB kernel stack.  A workspace
// may be reused after HttpsGet returns, but never concurrently or re-entrantly.
struct HttpsGetWorkspace {
    char current_url[kMaximumUpdateUrlBytes];
    ParsedUpdateUrl parsed_url;
    ParsedUpdateUrl redirect_url;
    HttpResponseHead response_head;
    char connect_error[160U];
    char request[kHttpsRequestBufferBytes];
    uint8_t headers[kMaximumHttpHeaderBytes];
    uint8_t receive[kHttpsReceiveBufferBytes];
};

class SecureByteStream {
 public:
    virtual ~SecureByteStream() {}
    virtual int Send(const uint8_t *data, size_t size) = 0;
    virtual int Receive(uint8_t *data, size_t capacity) = 0;
    virtual void Close() = 0;
};

class SecureStreamFactory {
 public:
    virtual ~SecureStreamFactory() {}
    // The returned stream must have completed a TLS handshake with SNI,
    // hostname verification, trusted CA validation and certificate-time
    // validation. Ownership remains with the factory until Destroy().
    virtual SecureByteStream *ConnectVerified(const char *host, uint16_t port,
                                               char *error,
                                               size_t error_size) = 0;
    virtual void Destroy(SecureByteStream *stream) = 0;
};

enum class HttpsRequestKind : uint8_t {
    GitHubApi = 0,
    ReleaseMetadata = 1,
    ReleaseZip = 2,
    GitHubDeviceFlow = 3
};

struct HttpsRequestOptions {
    const char *method;
    const char *accept;
    const char *content_type;
    ByteView body;
    // Optional GitHub bearer token. It is sent only to api.github.com on the
    // first request and is never forwarded across a redirect.
    const char *bearer_token;

    HttpsRequestOptions()
        : method("GET"), accept(0), content_type(0), body(),
          bearer_token(0) {}
};

enum class HttpsGetStatus : uint8_t {
    Ok = 0,
    InvalidArgument,
    UrlRejected,
    ConnectFailed,
    RequestTooLarge,
    SendFailed,
    ReceiveFailed,
    HeaderInvalid,
    RedirectRejected,
    TooManyRedirects,
    UnexpectedStatus,
    BodyInvalid,
    Canceled
};

struct HttpsGetResult {
    HttpsGetStatus status;
    UrlPolicyStatus url_status;
    HttpParseStatus header_status;
    HttpBodyStatus body_status;
    unsigned http_status;
    unsigned redirects;
    uint64_t body_bytes;
    char final_host[kMaximumUpdateHostBytes];
};

HttpsGetResult HttpsGet(const char *url, UpdateUrlPurpose initial_purpose,
                        HttpsRequestKind request_kind,
                        uint64_t maximum_body_bytes, HttpBodySink *sink,
                        SecureStreamFactory *factory,
                        HttpsGetWorkspace *workspace,
                        UpdateForegroundProgress *foreground_progress = 0);

HttpsGetResult HttpsRequest(
    const char *url, UpdateUrlPurpose initial_purpose,
    HttpsRequestKind request_kind, const HttpsRequestOptions &options,
    uint64_t maximum_body_bytes, HttpBodySink *sink,
    SecureStreamFactory *factory, HttpsGetWorkspace *workspace,
    UpdateForegroundProgress *foreground_progress = 0);

const char *HttpsGetStatusString(HttpsGetStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_HTTPS_STREAM_H
