#include "update/http_response_parser.h"

#include <limits.h>
#include <string.h>

namespace bmx {
namespace update {

namespace {

bool IsTokenCharacter(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
        return true;
    }
    return strchr("!#$%&'*+-.^_`|~", static_cast<int>(c)) != 0;
}

char AsciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool NameEquals(const uint8_t *name, size_t size, const char *expected) {
    const size_t expected_size = strlen(expected);
    if (size != expected_size) return false;
    for (size_t i = 0; i < size; ++i) {
        if (AsciiLower(static_cast<char>(name[i])) != expected[i]) return false;
    }
    return true;
}

bool ValueEqualsInsensitive(const uint8_t *value, size_t size,
                            const char *expected) {
    const size_t expected_size = strlen(expected);
    if (size != expected_size) return false;
    for (size_t i = 0; i < size; ++i) {
        if (AsciiLower(static_cast<char>(value[i])) != expected[i]) return false;
    }
    return true;
}

bool CopyValue(const uint8_t *value, size_t size, char *output,
               size_t output_size) {
    if (size >= output_size) return false;
    memcpy(output, value, size);
    output[size] = '\0';
    return true;
}

bool ParseDecimal(const uint8_t *value, size_t size, uint64_t *result) {
    if (size == 0U) return false;
    uint64_t parsed = 0U;
    for (size_t i = 0; i < size; ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(value[i] - '0');
        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    *result = parsed;
    return true;
}

const uint8_t *FindCrlf(const uint8_t *begin, const uint8_t *end) {
    for (const uint8_t *p = begin; p + 1 < end; ++p) {
        if (p[0] == '\r' && p[1] == '\n') return p;
        if (p[0] == '\n') return 0;
    }
    return 0;
}

bool StatusMayHaveUnframedBody(unsigned status) {
    return (status >= 100U && status < 200U) || status == 204U ||
           status == 304U || (status >= 300U && status < 400U);
}

}  // namespace

HttpParseStatus ParseHttpResponseHead(const uint8_t *input,
                                      size_t input_size,
                                      HttpResponseHead *head,
                                      size_t *header_bytes) {
    if (input == 0 || head == 0 || header_bytes == 0) {
        return HttpParseStatus::InvalidArgument;
    }
    if (input_size > kMaximumHttpHeaderBytes) {
        input_size = kMaximumHttpHeaderBytes;
    }

    const uint8_t *begin = input;
    const uint8_t *end = input + input_size;
    const uint8_t *line_end = FindCrlf(begin, end);
    if (line_end == 0) {
        return input_size == kMaximumHttpHeaderBytes
                   ? HttpParseStatus::HeaderTooLarge
                   : HttpParseStatus::NeedMoreData;
    }
    const size_t status_size = static_cast<size_t>(line_end - begin);
    if (status_size < 12U ||
        (memcmp(begin, "HTTP/1.1 ", 9U) != 0 &&
         memcmp(begin, "HTTP/1.0 ", 9U) != 0) ||
        begin[9] < '1' || begin[9] > '5' ||
        begin[10] < '0' || begin[10] > '9' ||
        begin[11] < '0' || begin[11] > '9' ||
        (status_size > 12U && begin[12] != ' ')) {
        return HttpParseStatus::InvalidStatusLine;
    }
    // BMX does not need RFC 9110's obsolete/non-ASCII reason-phrase forms.
    // Reject controls (especially NUL), DEL and high bytes so no downstream
    // C-string consumer can observe a different status line than this parser.
    for (size_t i = 12U; i < status_size; ++i) {
        if (begin[i] < 0x20U || begin[i] > 0x7eU) {
            return HttpParseStatus::InvalidStatusLine;
        }
    }

    memset(head, 0, sizeof(*head));
    head->status_code = static_cast<unsigned>((begin[9] - '0') * 100 +
                                              (begin[10] - '0') * 10 +
                                              (begin[11] - '0'));

    bool saw_transfer_encoding = false;
    bool saw_content_encoding = false;
    bool saw_location = false;
    bool saw_content_type = false;
    bool saw_connection = false;
    const uint8_t *line = line_end + 2;
    for (;;) {
        if (line + 1 >= end) {
            return input_size == kMaximumHttpHeaderBytes
                       ? HttpParseStatus::HeaderTooLarge
                       : HttpParseStatus::NeedMoreData;
        }
        if (line[0] == '\r' && line[1] == '\n') {
            *header_bytes = static_cast<size_t>(line + 2 - begin);
            break;
        }
        if (line[0] == ' ' || line[0] == '\t') {
            return HttpParseStatus::InvalidHeader;
        }
        line_end = FindCrlf(line, end);
        if (line_end == 0) {
            return input_size == kMaximumHttpHeaderBytes
                       ? HttpParseStatus::HeaderTooLarge
                       : HttpParseStatus::NeedMoreData;
        }
        const uint8_t *colon = static_cast<const uint8_t *>(
            memchr(line, ':', static_cast<size_t>(line_end - line)));
        if (colon == 0 || colon == line) return HttpParseStatus::InvalidHeader;
        for (const uint8_t *p = line; p < colon; ++p) {
            if (!IsTokenCharacter(*p)) return HttpParseStatus::InvalidHeader;
        }
        const uint8_t *value = colon + 1;
        while (value < line_end && (*value == ' ' || *value == '\t')) ++value;
        const uint8_t *value_end = line_end;
        while (value_end > value &&
               (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            --value_end;
        }
        for (const uint8_t *p = value; p < value_end; ++p) {
            if ((*p < 0x20U && *p != '\t') || *p == 0x7fU) {
                return HttpParseStatus::InvalidHeader;
            }
        }
        const size_t name_size = static_cast<size_t>(colon - line);
        const size_t value_size = static_cast<size_t>(value_end - value);

        if (NameEquals(line, name_size, "content-length")) {
            if (head->has_content_length) return HttpParseStatus::DuplicateHeader;
            if (!ParseDecimal(value, value_size, &head->content_length)) {
                return HttpParseStatus::InvalidContentLength;
            }
            head->has_content_length = true;
        } else if (NameEquals(line, name_size, "transfer-encoding")) {
            if (saw_transfer_encoding) return HttpParseStatus::DuplicateHeader;
            saw_transfer_encoding = true;
            if (!ValueEqualsInsensitive(value, value_size, "chunked")) {
                return HttpParseStatus::UnsupportedTransferEncoding;
            }
            head->chunked = true;
        } else if (NameEquals(line, name_size, "content-encoding")) {
            if (saw_content_encoding) return HttpParseStatus::DuplicateHeader;
            saw_content_encoding = true;
            if (!ValueEqualsInsensitive(value, value_size, "identity")) {
                return HttpParseStatus::UnsupportedContentEncoding;
            }
        } else if (NameEquals(line, name_size, "location")) {
            if (saw_location) return HttpParseStatus::DuplicateHeader;
            saw_location = true;
            if (!CopyValue(value, value_size, head->location,
                           sizeof(head->location))) {
                return HttpParseStatus::OutputTooSmall;
            }
        } else if (NameEquals(line, name_size, "content-type")) {
            if (saw_content_type) return HttpParseStatus::DuplicateHeader;
            saw_content_type = true;
            if (!CopyValue(value, value_size, head->content_type,
                           sizeof(head->content_type))) {
                return HttpParseStatus::OutputTooSmall;
            }
        } else if (NameEquals(line, name_size, "connection")) {
            if (saw_connection) return HttpParseStatus::DuplicateHeader;
            saw_connection = true;
            head->connection_close =
                ValueEqualsInsensitive(value, value_size, "close");
        } else if (NameEquals(line, name_size, "content-range")) {
            return HttpParseStatus::InvalidHeader;
        }
        line = line_end + 2;
    }

    if (head->has_content_length && head->chunked) {
        return HttpParseStatus::AmbiguousBodyLength;
    }
    if (!head->has_content_length && !head->chunked &&
        !StatusMayHaveUnframedBody(head->status_code)) {
        return HttpParseStatus::MissingBodyLength;
    }
    if (head->status_code >= 300U && head->status_code < 400U &&
        head->location[0] == '\0') {
        return HttpParseStatus::MissingLocation;
    }
    return HttpParseStatus::Ok;
}

HttpBodyDecoder::HttpBodyDecoder(const HttpResponseHead &head,
                                 uint64_t maximum_body_bytes,
                                 HttpBodySink *sink)
    : state_(head.chunked ? kChunkSize : kContentLength),
      remaining_(head.has_content_length ? head.content_length : 0U),
      decoded_bytes_(0U),
      maximum_body_bytes_(maximum_body_bytes),
      sink_(sink),
      chunk_line_size_(0U) {
    if (sink_ == 0 || (head.has_content_length == head.chunked)) {
        state_ = kFailed;
    } else if (!head.chunked && remaining_ > maximum_body_bytes_) {
        state_ = kFailed;
    } else if (!head.chunked && remaining_ == 0U) {
        state_ = kComplete;
    }
}

HttpBodyStatus HttpBodyDecoder::Fail(HttpBodyStatus status) {
    state_ = kFailed;
    return status;
}

HttpBodyStatus HttpBodyDecoder::Write(const uint8_t *data, size_t size) {
    if (size == 0U) return HttpBodyStatus::Ok;
    if (decoded_bytes_ > maximum_body_bytes_ ||
        static_cast<uint64_t>(size) > maximum_body_bytes_ - decoded_bytes_) {
        return Fail(HttpBodyStatus::TooLarge);
    }
    if (!sink_->Write(data, size)) return Fail(HttpBodyStatus::SinkRejected);
    decoded_bytes_ += static_cast<uint64_t>(size);
    return HttpBodyStatus::Ok;
}

HttpBodyStatus HttpBodyDecoder::Feed(const uint8_t *data, size_t size,
                                     size_t *consumed) {
    if (consumed == 0 || (data == 0 && size != 0U)) {
        return HttpBodyStatus::InvalidArgument;
    }
    *consumed = 0U;
    if (state_ == kFailed) return HttpBodyStatus::InvalidArgument;
    if (state_ == kComplete) {
        return size == 0U ? HttpBodyStatus::Complete
                          : HttpBodyStatus::UnexpectedData;
    }

    while (*consumed < size) {
        if (state_ == kContentLength) {
            size_t available = size - *consumed;
            if (static_cast<uint64_t>(available) > remaining_) {
                available = static_cast<size_t>(remaining_);
            }
            const HttpBodyStatus write_status = Write(data + *consumed, available);
            if (write_status != HttpBodyStatus::Ok) return write_status;
            *consumed += available;
            remaining_ -= static_cast<uint64_t>(available);
            if (remaining_ == 0U) {
                state_ = kComplete;
                return *consumed == size ? HttpBodyStatus::Complete
                                         : HttpBodyStatus::UnexpectedData;
            }
            continue;
        }

        const uint8_t c = data[(*consumed)++];
        if (state_ == kChunkSize) {
            if (c == '\r') {
                if (chunk_line_size_ == 0U) return Fail(HttpBodyStatus::InvalidChunk);
                uint64_t chunk_size = 0U;
                for (size_t i = 0; i < chunk_line_size_; ++i) {
                    const char digit_char = chunk_line_[i];
                    unsigned digit;
                    if (digit_char >= '0' && digit_char <= '9') {
                        digit = static_cast<unsigned>(digit_char - '0');
                    } else if (digit_char >= 'a' && digit_char <= 'f') {
                        digit = static_cast<unsigned>(digit_char - 'a' + 10);
                    } else if (digit_char >= 'A' && digit_char <= 'F') {
                        digit = static_cast<unsigned>(digit_char - 'A' + 10);
                    } else {
                        return Fail(HttpBodyStatus::InvalidChunk);
                    }
                    if (chunk_size > (UINT64_MAX - digit) / 16U) {
                        return Fail(HttpBodyStatus::InvalidChunk);
                    }
                    chunk_size = chunk_size * 16U + digit;
                }
                remaining_ = chunk_size;
                chunk_line_size_ = 0U;
                state_ = kChunkSizeLf;
                continue;
            }
            if (c == '\n' || c == ';' || chunk_line_size_ >= sizeof(chunk_line_)) {
                return Fail(HttpBodyStatus::InvalidChunk);
            }
            chunk_line_[chunk_line_size_++] = static_cast<char>(c);
            continue;
        }
        if (state_ == kChunkSizeLf) {
            if (c != '\n') return Fail(HttpBodyStatus::InvalidChunk);
            state_ = remaining_ == 0U ? kFinalCr : kChunkData;
            continue;
        }
        if (state_ == kChunkData) {
            // We consumed one byte above, so write it and then bulk-copy the rest.
            HttpBodyStatus write_status = Write(&c, 1U);
            if (write_status != HttpBodyStatus::Ok) return write_status;
            --remaining_;
            if (remaining_ != 0U && *consumed < size) {
                size_t available = size - *consumed;
                if (static_cast<uint64_t>(available) > remaining_) {
                    available = static_cast<size_t>(remaining_);
                }
                write_status = Write(data + *consumed, available);
                if (write_status != HttpBodyStatus::Ok) return write_status;
                *consumed += available;
                remaining_ -= static_cast<uint64_t>(available);
            }
            if (remaining_ == 0U) state_ = kChunkDataCr;
            continue;
        }
        if (state_ == kChunkDataCr) {
            if (c != '\r') return Fail(HttpBodyStatus::InvalidChunk);
            state_ = kChunkDataLf;
            continue;
        }
        if (state_ == kChunkDataLf) {
            if (c != '\n') return Fail(HttpBodyStatus::InvalidChunk);
            state_ = kChunkSize;
            continue;
        }
        if (state_ == kFinalCr) {
            if (c != '\r') return Fail(HttpBodyStatus::InvalidChunk);
            state_ = kFinalLf;
            continue;
        }
        if (state_ == kFinalLf) {
            if (c != '\n') return Fail(HttpBodyStatus::InvalidChunk);
            state_ = kComplete;
            return *consumed == size ? HttpBodyStatus::Complete
                                     : HttpBodyStatus::UnexpectedData;
        }
    }
    return state_ == kComplete ? HttpBodyStatus::Complete : HttpBodyStatus::Ok;
}

HttpBodyStatus HttpBodyDecoder::Finish() {
    if (state_ == kComplete) return HttpBodyStatus::Complete;
    if (state_ == kFailed) return HttpBodyStatus::InvalidArgument;
    return Fail(HttpBodyStatus::Truncated);
}

const char *HttpParseStatusString(HttpParseStatus status) {
    switch (status) {
        case HttpParseStatus::Ok: return "ok";
        case HttpParseStatus::NeedMoreData: return "need more HTTP header data";
        case HttpParseStatus::InvalidArgument: return "invalid argument";
        case HttpParseStatus::HeaderTooLarge: return "HTTP header too large";
        case HttpParseStatus::InvalidStatusLine: return "invalid HTTP status line";
        case HttpParseStatus::InvalidHeader: return "invalid HTTP header";
        case HttpParseStatus::DuplicateHeader: return "duplicate HTTP header";
        case HttpParseStatus::UnsupportedTransferEncoding: return "unsupported transfer encoding";
        case HttpParseStatus::UnsupportedContentEncoding: return "unsupported content encoding";
        case HttpParseStatus::AmbiguousBodyLength: return "ambiguous HTTP body length";
        case HttpParseStatus::MissingBodyLength: return "missing HTTP body length";
        case HttpParseStatus::InvalidContentLength: return "invalid content length";
        case HttpParseStatus::MissingLocation: return "redirect location missing";
        case HttpParseStatus::OutputTooSmall: return "HTTP field too large";
    }
    return "unknown HTTP parse error";
}

const char *HttpBodyStatusString(HttpBodyStatus status) {
    switch (status) {
        case HttpBodyStatus::Ok: return "ok";
        case HttpBodyStatus::Complete: return "complete";
        case HttpBodyStatus::InvalidArgument: return "invalid argument";
        case HttpBodyStatus::TooLarge: return "HTTP body too large";
        case HttpBodyStatus::SinkRejected: return "HTTP body sink rejected data";
        case HttpBodyStatus::InvalidChunk: return "invalid chunked HTTP body";
        case HttpBodyStatus::UnexpectedData: return "data after HTTP body";
        case HttpBodyStatus::Truncated: return "truncated HTTP body";
    }
    return "unknown HTTP body error";
}

}  // namespace update
}  // namespace bmx
