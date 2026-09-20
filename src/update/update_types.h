#ifndef BMX_UPDATE_UPDATE_TYPES_H
#define BMX_UPDATE_UPDATE_TYPES_H

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace update {

static const size_t kSha256DigestBytes = 32U;

enum class BoardFamily : uint8_t {
    Unknown = 0,
    Pi4Pi400 = 1,
    Pi5Pi500 = 2
};

enum class ReleaseChannel : uint8_t {
    Unknown = 0,
    Stable = 1,
    Prerelease = 2
};

struct ByteView {
    const uint8_t *data;
    size_t size;

    ByteView() : data(0), size(0) {}
    ByteView(const uint8_t *bytes, size_t length) : data(bytes), size(length) {}
};

bool IsKnownBoardFamily(BoardFamily board);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_UPDATE_TYPES_H
