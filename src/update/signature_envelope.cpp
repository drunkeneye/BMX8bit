#include "update/signature_envelope.h"

#include <string.h>

namespace bmx {
namespace update {

namespace {

static const char kEnvelopeHeader[] = "BMX-SIGNATURE-V1";
static const char kKeyPrefix[] = "key-id: ";
static const char kAlgorithmLine[] = "algorithm: ECDSA-P256-SHA256";
static const char kSignaturePrefix[] = "signature: ";

bool IsIdentifier(const uint8_t *value, size_t size) {
    if (size == 0U || size > kMaximumSigningKeyIdBytes) return false;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t c = value[i];
        const bool alpha_numeric = (c >= '0' && c <= '9') ||
                                   (c >= 'A' && c <= 'Z') ||
                                   (c >= 'a' && c <= 'z');
        if (!alpha_numeric && (i == 0U || (c != '.' && c != '_' && c != '-'))) {
            return false;
        }
    }
    return true;
}

int Base64Value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool DecodeCanonicalBase64(const uint8_t *encoded, size_t encoded_size,
                           uint8_t *output, size_t output_capacity,
                           size_t *output_size) {
    if (encoded_size == 0U || (encoded_size & 3U) != 0U) return false;
    size_t padding = 0U;
    if (encoded[encoded_size - 1U] == '=') ++padding;
    if (encoded_size >= 2U && encoded[encoded_size - 2U] == '=') ++padding;
    const size_t decoded_size = encoded_size / 4U * 3U - padding;
    if (decoded_size > output_capacity) return false;
    size_t out = 0U;
    for (size_t i = 0; i < encoded_size; i += 4U) {
        const bool final_group = i + 4U == encoded_size;
        int values[4];
        values[0] = Base64Value(encoded[i]);
        values[1] = Base64Value(encoded[i + 1U]);
        values[2] = encoded[i + 2U] == '=' ? 0 : Base64Value(encoded[i + 2U]);
        values[3] = encoded[i + 3U] == '=' ? 0 : Base64Value(encoded[i + 3U]);
        if (values[0] < 0 || values[1] < 0 || values[2] < 0 || values[3] < 0) {
            return false;
        }
        if (!final_group &&
            (encoded[i + 2U] == '=' || encoded[i + 3U] == '=')) return false;
        if (encoded[i + 2U] == '=' && encoded[i + 3U] != '=') return false;
        const uint32_t block = static_cast<uint32_t>(values[0] << 18) |
                               static_cast<uint32_t>(values[1] << 12) |
                               static_cast<uint32_t>(values[2] << 6) |
                               static_cast<uint32_t>(values[3]);
        if (out < decoded_size) output[out++] = static_cast<uint8_t>(block >> 16);
        if (out < decoded_size) output[out++] = static_cast<uint8_t>(block >> 8);
        if (out < decoded_size) output[out++] = static_cast<uint8_t>(block);
        if (final_group && padding == 1U && (values[2] & 0x03) != 0) return false;
        if (final_group && padding == 2U && (values[1] & 0x0f) != 0) return false;
    }
    *output_size = out;
    return out == decoded_size;
}

bool ScalarInP256Range(const uint8_t *encoded, size_t size) {
    if (size == 0U || size > 33U || (encoded[0] & 0x80U) != 0U) return false;
    if (size > 1U && encoded[0] == 0U && (encoded[1] & 0x80U) == 0U) return false;
    if (size == 33U && encoded[0] != 0U) return false;
    const uint8_t *value = encoded;
    if (size == 33U) {
        ++value;
        --size;
    }
    while (size > 0U && *value == 0U) {
        ++value;
        --size;
    }
    if (size == 0U) return false;
    static const uint8_t kP256Order[32] = {
        0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
        0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51
    };
    if (size < sizeof(kP256Order)) return true;
    if (size > sizeof(kP256Order)) return false;
    return memcmp(value, kP256Order, sizeof(kP256Order)) < 0;
}

struct Line {
    const uint8_t *data;
    size_t size;
};

bool NextLine(ByteView envelope, size_t *cursor, Line *line) {
    const size_t start = *cursor;
    size_t end = start;
    while (end < envelope.size && envelope.data[end] != '\n') ++end;
    if (end >= envelope.size) return false;
    line->data = envelope.data + start;
    line->size = end - start;
    *cursor = end + 1U;
    return true;
}

bool LineEquals(const Line &line, const char *expected) {
    const size_t size = strlen(expected);
    return line.size == size && memcmp(line.data, expected, size) == 0;
}

bool LineStartsWith(const Line &line, const char *prefix,
                    const uint8_t **value, size_t *value_size) {
    const size_t prefix_size = strlen(prefix);
    if (line.size <= prefix_size || memcmp(line.data, prefix, prefix_size) != 0) {
        return false;
    }
    *value = line.data + prefix_size;
    *value_size = line.size - prefix_size;
    return true;
}

}  // namespace

bool IsValidP256EcdsaDer(ByteView signature) {
    if (signature.data == 0 || signature.size < 8U ||
        signature.size > kMaximumEcdsaDerBytes || signature.data[0] != 0x30U ||
        signature.data[1] >= 0x80U ||
        static_cast<size_t>(signature.data[1]) != signature.size - 2U) {
        return false;
    }
    size_t cursor = 2U;
    for (unsigned scalar = 0U; scalar < 2U; ++scalar) {
        if (cursor + 2U > signature.size || signature.data[cursor++] != 0x02U) {
            return false;
        }
        const uint8_t size = signature.data[cursor++];
        if (size == 0U || size > 33U || cursor + size > signature.size ||
            !ScalarInP256Range(signature.data + cursor, size)) {
            return false;
        }
        cursor += size;
    }
    return cursor == signature.size;
}

SignatureEnvelopeStatus ParseSignatureEnvelope(
    ByteView envelope, ReleaseSignature *signatures, size_t signature_capacity,
    size_t *signature_count) {
    if (envelope.data == 0 || signatures == 0 || signature_count == 0) {
        return SignatureEnvelopeStatus::InvalidArgument;
    }
    *signature_count = 0U;
    if (envelope.size == 0U || envelope.size > kMaximumSignatureEnvelopeBytes) {
        return SignatureEnvelopeStatus::InvalidSize;
    }
    if (envelope.data[envelope.size - 1U] != '\n') {
        return SignatureEnvelopeStatus::InvalidLineEnding;
    }
    for (size_t i = 0; i < envelope.size; ++i) {
        if (envelope.data[i] == '\r') return SignatureEnvelopeStatus::InvalidLineEnding;
        if (envelope.data[i] > 0x7fU) return SignatureEnvelopeStatus::NonAscii;
    }

    size_t cursor = 0U;
    Line line;
    if (!NextLine(envelope, &cursor, &line) || !LineEquals(line, kEnvelopeHeader)) {
        return SignatureEnvelopeStatus::UnsupportedVersion;
    }
    size_t count = 0U;
    char previous_key[kMaximumSigningKeyIdBytes + 1U] = {0};
    while (cursor < envelope.size) {
        if (count >= kMaximumReleaseSignatures || count >= signature_capacity) {
            return SignatureEnvelopeStatus::SignatureCount;
        }
        Line key_line;
        Line algorithm_line;
        Line signature_line;
        if (!NextLine(envelope, &cursor, &key_line) ||
            !NextLine(envelope, &cursor, &algorithm_line) ||
            !NextLine(envelope, &cursor, &signature_line)) {
            return SignatureEnvelopeStatus::IncompleteRecord;
        }
        const uint8_t *key = 0;
        size_t key_size = 0U;
        if (!LineStartsWith(key_line, kKeyPrefix, &key, &key_size) ||
            !IsIdentifier(key, key_size)) {
            return SignatureEnvelopeStatus::InvalidKeyId;
        }
        memcpy(signatures[count].key_id, key, key_size);
        signatures[count].key_id[key_size] = '\0';
        if (count != 0U && strcmp(previous_key, signatures[count].key_id) >= 0) {
            return SignatureEnvelopeStatus::KeyOrder;
        }
        memcpy(previous_key, signatures[count].key_id, key_size + 1U);
        if (!LineEquals(algorithm_line, kAlgorithmLine)) {
            return SignatureEnvelopeStatus::UnsupportedAlgorithm;
        }
        const uint8_t *encoded = 0;
        size_t encoded_size = 0U;
        if (!LineStartsWith(signature_line, kSignaturePrefix, &encoded,
                            &encoded_size) ||
            !DecodeCanonicalBase64(encoded, encoded_size, signatures[count].der,
                                   sizeof(signatures[count].der),
                                   &signatures[count].der_size)) {
            return SignatureEnvelopeStatus::InvalidBase64;
        }
        if (!IsValidP256EcdsaDer(
                ByteView(signatures[count].der, signatures[count].der_size))) {
            return SignatureEnvelopeStatus::InvalidDer;
        }
        ++count;
    }
    if (count == 0U) return SignatureEnvelopeStatus::SignatureCount;
    *signature_count = count;
    return SignatureEnvelopeStatus::Ok;
}

const char *SignatureEnvelopeStatusString(SignatureEnvelopeStatus status) {
    switch (status) {
        case SignatureEnvelopeStatus::Ok: return "ok";
        case SignatureEnvelopeStatus::InvalidArgument: return "invalid argument";
        case SignatureEnvelopeStatus::InvalidSize: return "signature envelope size invalid";
        case SignatureEnvelopeStatus::NonAscii: return "signature envelope is not ASCII";
        case SignatureEnvelopeStatus::InvalidLineEnding: return "signature envelope line ending invalid";
        case SignatureEnvelopeStatus::UnsupportedVersion: return "unsupported signature envelope";
        case SignatureEnvelopeStatus::IncompleteRecord: return "incomplete signature record";
        case SignatureEnvelopeStatus::SignatureCount: return "signature count invalid";
        case SignatureEnvelopeStatus::InvalidKeyId: return "invalid signature key ID";
        case SignatureEnvelopeStatus::KeyOrder: return "signature key IDs not sorted and unique";
        case SignatureEnvelopeStatus::UnsupportedAlgorithm: return "unsupported signature algorithm";
        case SignatureEnvelopeStatus::InvalidBase64: return "invalid signature Base64";
        case SignatureEnvelopeStatus::InvalidDer: return "invalid P-256 ECDSA DER";
    }
    return "unknown signature envelope error";
}

}  // namespace update
}  // namespace bmx
