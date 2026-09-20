#include "update/sha256.h"

#include <string.h>

namespace bmx {
namespace update {

namespace {

uint32_t RotateRight(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

uint32_t LoadBigEndian32(const uint8_t *input) {
    return (static_cast<uint32_t>(input[0]) << 24U) |
           (static_cast<uint32_t>(input[1]) << 16U) |
           (static_cast<uint32_t>(input[2]) << 8U) |
           static_cast<uint32_t>(input[3]);
}

void StoreBigEndian32(uint32_t value, uint8_t *output) {
    output[0] = static_cast<uint8_t>(value >> 24U);
    output[1] = static_cast<uint8_t>(value >> 16U);
    output[2] = static_cast<uint8_t>(value >> 8U);
    output[3] = static_cast<uint8_t>(value);
}

static const uint32_t kRoundConstants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

}  // namespace

Sha256::Sha256() { Reset(); }

void Sha256::Reset() {
    state_[0] = 0x6a09e667U;
    state_[1] = 0xbb67ae85U;
    state_[2] = 0x3c6ef372U;
    state_[3] = 0xa54ff53aU;
    state_[4] = 0x510e527fU;
    state_[5] = 0x9b05688cU;
    state_[6] = 0x1f83d9abU;
    state_[7] = 0x5be0cd19U;
    total_bytes_ = 0U;
    buffer_size_ = 0U;
    finalized_ = false;
    memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::Transform(const uint8_t block[64]) {
    uint32_t words[64];
    for (unsigned i = 0U; i < 16U; ++i) {
        words[i] = LoadBigEndian32(block + i * 4U);
    }
    for (unsigned i = 16U; i < 64U; ++i) {
        const uint32_t s0 = RotateRight(words[i - 15U], 7U) ^
                            RotateRight(words[i - 15U], 18U) ^
                            (words[i - 15U] >> 3U);
        const uint32_t s1 = RotateRight(words[i - 2U], 17U) ^
                            RotateRight(words[i - 2U], 19U) ^
                            (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (unsigned i = 0U; i < 64U; ++i) {
        const uint32_t sum1 = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^
                              RotateRight(e, 25U);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t temporary1 = h + sum1 + choice + kRoundConstants[i] +
                                    words[i];
        const uint32_t sum0 = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^
                              RotateRight(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
    memset(words, 0, sizeof(words));
}

bool Sha256::Update(const void *data, size_t size) {
    if (finalized_ || (data == 0 && size != 0U) ||
        static_cast<uint64_t>(size) > UINT64_MAX - total_bytes_) {
        return false;
    }
    const uint8_t *input = static_cast<const uint8_t *>(data);
    total_bytes_ += static_cast<uint64_t>(size);
    while (size != 0U) {
        size_t copy_size = sizeof(buffer_) - buffer_size_;
        if (copy_size > size) copy_size = size;
        memcpy(buffer_ + buffer_size_, input, copy_size);
        buffer_size_ += copy_size;
        input += copy_size;
        size -= copy_size;
        if (buffer_size_ == sizeof(buffer_)) {
            Transform(buffer_);
            buffer_size_ = 0U;
        }
    }
    return true;
}

bool Sha256::Final(uint8_t digest[kSha256DigestBytes]) {
    if (digest == 0 || finalized_ || total_bytes_ > UINT64_MAX / 8U) {
        return false;
    }
    const uint64_t total_bits = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80U;
    if (buffer_size_ > 56U) {
        memset(buffer_ + buffer_size_, 0, sizeof(buffer_) - buffer_size_);
        Transform(buffer_);
        buffer_size_ = 0U;
    }
    memset(buffer_ + buffer_size_, 0, 56U - buffer_size_);
    for (unsigned i = 0U; i < 8U; ++i) {
        buffer_[63U - i] = static_cast<uint8_t>(total_bits >> (i * 8U));
    }
    Transform(buffer_);
    for (unsigned i = 0U; i < 8U; ++i) {
        StoreBigEndian32(state_[i], digest + i * 4U);
    }
    finalized_ = true;
    memset(buffer_, 0, sizeof(buffer_));
    return true;
}

bool Sha256Digest(ByteView input, uint8_t digest[kSha256DigestBytes]) {
    if (digest == 0 || (input.data == 0 && input.size != 0U)) return false;
    Sha256 sha256;
    return sha256.Update(input.data, input.size) && sha256.Final(digest);
}

bool ConstantTimeDigestEqual(const uint8_t left[kSha256DigestBytes],
                             const uint8_t right[kSha256DigestBytes]) {
    if (left == 0 || right == 0) return false;
    uint8_t difference = 0U;
    for (size_t i = 0U; i < kSha256DigestBytes; ++i) {
        difference = static_cast<uint8_t>(difference | (left[i] ^ right[i]));
    }
    return difference == 0U;
}

}  // namespace update
}  // namespace bmx
