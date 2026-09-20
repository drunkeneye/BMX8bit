#ifndef BMX_UPDATE_SHA256_H
#define BMX_UPDATE_SHA256_H

#include "update/update_types.h"

namespace bmx {
namespace update {

class Sha256 {
 public:
    Sha256();

    void Reset();
    bool Update(const void *data, size_t size);
    bool Final(uint8_t digest[kSha256DigestBytes]);

 private:
    void Transform(const uint8_t block[64]);

    uint32_t state_[8];
    uint64_t total_bytes_;
    uint8_t buffer_[64];
    size_t buffer_size_;
    bool finalized_;
};

bool Sha256Digest(ByteView input, uint8_t digest[kSha256DigestBytes]);
bool ConstantTimeDigestEqual(const uint8_t left[kSha256DigestBytes],
                             const uint8_t right[kSha256DigestBytes]);

}  // namespace update
}  // namespace bmx

#endif  // BMX_UPDATE_SHA256_H
