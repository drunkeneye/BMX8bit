#include "update/body_sinks.h"

#include <string.h>

namespace bmx {
namespace update {

BoundedMemoryBodySink::BoundedMemoryBodySink(uint8_t *buffer, size_t capacity)
    : buffer_(buffer), capacity_(capacity), size_(0U), overflowed_(false) {}

bool BoundedMemoryBodySink::Write(const uint8_t *data, size_t size) {
    if ((data == 0 && size != 0U) || buffer_ == 0 || size > capacity_ - size_) {
        overflowed_ = true;
        return false;
    }
    if (size != 0U) memcpy(buffer_ + size_, data, size);
    size_ += size;
    return true;
}

void BoundedMemoryBodySink::Reset() {
    size_ = 0U;
    overflowed_ = false;
}

}  // namespace update
}  // namespace bmx
