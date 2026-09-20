#ifndef BMX_UPDATE_HTTP_RESPONSE_PARSER_H
#define BMX_UPDATE_HTTP_RESPONSE_PARSER_H

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace update {

static const size_t kMaximumHttpHeaderBytes = 32768U;
static const size_t kMaximumHttpLocationBytes = 2048U;
static const size_t kMaximumHttpContentTypeBytes = 96U;

enum class HttpParseStatus : uint8_t {
    Ok = 0,
    NeedMoreData,
    InvalidArgument,
    HeaderTooLarge,
    InvalidStatusLine,
    InvalidHeader,
    DuplicateHeader,
    UnsupportedTransferEncoding,
    UnsupportedContentEncoding,
    AmbiguousBodyLength,
    MissingBodyLength,
    InvalidContentLength,
    MissingLocation,
    OutputTooSmall
};

struct HttpResponseHead {
    unsigned status_code;
    bool has_content_length;
    uint64_t content_length;
    bool chunked;
    bool connection_close;
    char location[kMaximumHttpLocationBytes];
    char content_type[kMaximumHttpContentTypeBytes];
};

HttpParseStatus ParseHttpResponseHead(const uint8_t *input,
                                      size_t input_size,
                                      HttpResponseHead *head,
                                      size_t *header_bytes);

class HttpBodySink {
 public:
    virtual ~HttpBodySink() {}
    virtual bool Write(const uint8_t *data, size_t size) = 0;
};

enum class HttpBodyStatus : uint8_t {
    Ok = 0,
    Complete,
    InvalidArgument,
    TooLarge,
    SinkRejected,
    InvalidChunk,
    UnexpectedData,
    Truncated
};

// Stateful decoder for an exactly framed Content-Length or chunked body.
// The caller may pass arbitrarily split network buffers. Trailers and chunk
// extensions are deliberately rejected because BMX never needs them.
class HttpBodyDecoder {
 public:
    HttpBodyDecoder(const HttpResponseHead &head, uint64_t maximum_body_bytes,
                    HttpBodySink *sink);

    HttpBodyStatus Feed(const uint8_t *data, size_t size, size_t *consumed);
    HttpBodyStatus Finish();
    uint64_t decoded_bytes() const { return decoded_bytes_; }
    bool complete() const { return state_ == kComplete; }

 private:
    enum State : uint8_t {
        kContentLength,
        kChunkSize,
        kChunkSizeLf,
        kChunkData,
        kChunkDataCr,
        kChunkDataLf,
        kFinalCr,
        kFinalLf,
        kComplete,
        kFailed
    };

    HttpBodyStatus Fail(HttpBodyStatus status);
    HttpBodyStatus Write(const uint8_t *data, size_t size);

    State state_;
    uint64_t remaining_;
    uint64_t decoded_bytes_;
    uint64_t maximum_body_bytes_;
    HttpBodySink *sink_;
    char chunk_line_[18];
    size_t chunk_line_size_;
};

const char *HttpParseStatusString(HttpParseStatus status);
const char *HttpBodyStatusString(HttpBodyStatus status);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_HTTP_RESPONSE_PARSER_H
