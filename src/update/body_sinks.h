#ifndef BMX_UPDATE_BODY_SINKS_H
#define BMX_UPDATE_BODY_SINKS_H

#include "update/http_response_parser.h"
#include "update/update_types.h"

namespace bmx {
namespace update {

class BoundedMemoryBodySink : public HttpBodySink {
 public:
    BoundedMemoryBodySink(uint8_t *buffer, size_t capacity);
    bool Write(const uint8_t *data, size_t size) override;
    void Reset();
    ByteView bytes() const { return ByteView(buffer_, size_); }
    bool overflowed() const { return overflowed_; }

 private:
    uint8_t *buffer_;
    size_t capacity_;
    size_t size_;
    bool overflowed_;
};

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_BODY_SINKS_H
