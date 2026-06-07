#include "impl/scalar.h"
#include "impl/scalar/scalar_common.h"
#include "unidata.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCALAR_UTF16_IMPLEMENTATION(endianness, is_big_endian)                 \
  static size_t scalar_casefold_character_utf16##endianness##_bmp(             \
      uint16_t c, uint8_t *out) {                                              \
    uint16_t shifted = c >> 6;                                                 \
    uint16_t masked = c & 63;                                                  \
    uint16_t index = UNIDATA_UTF16_CASEFOLD_TRIE_INDEX[shifted];               \
    uint16_t value = UNIDATA_UTF16_CASEFOLD_TRIE_DATA[index + masked];         \
    if (value == 0) {                                                          \
      return 0;                                                                \
    }                                                                          \
    uint8_t length = value >> 12;                                              \
    size_t offset = value & 0xFFF;                                             \
    const uint8_t *bytes = &UNIDATA_UTF16_CASEFOLD_DATA[offset];               \
    for (size_t k = 0; k < length; k += 2) {                                   \
      if (is_big_endian) {                                                     \
        out[0] = bytes[k + 1];                                                 \
        out[1] = bytes[k];                                                     \
      } else {                                                                 \
        out[0] = bytes[k];                                                     \
        out[1] = bytes[k + 1];                                                 \
      }                                                                        \
      out += 2;                                                                \
    }                                                                          \
    return length;                                                             \
  }                                                                            \
                                                                               \
  static size_t scalar_casefold_character_utf16##endianness##_supplementary(   \
      uint32_t c, uint8_t *out) {                                              \
    uint32_t salt_hash =                                                       \
        scalar_phash(c, 0, sizeof(UNIDATA_CASEFOLD_KV) / sizeof(uint32_t));    \
    uint32_t salt = UNIDATA_CASEFOLD_SALT[salt_hash];                          \
    uint32_t key_hash =                                                        \
        scalar_phash(c, salt, sizeof(UNIDATA_CASEFOLD_KV) / sizeof(uint32_t)); \
    uint32_t k = UNIDATA_CASEFOLD_KV[key_hash][1];                             \
    uint32_t casefold = UNIDATA_CASEFOLD_KV[key_hash][0];                      \
    if (k == c) {                                                              \
      return scalar_write_code_point_utf16##endianness(casefold, out);         \
    }                                                                          \
    return 0;                                                                  \
  }                                                                            \
                                                                               \
  size_t scalar_casefold_utf16##endianness(const uint8_t *input,               \
                                           size_t length, uint8_t *out) {      \
    uint8_t *start = out;                                                      \
                                                                               \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint16_t code_unit = scalar_read_uint16##endianness(input + p);          \
      if (!scalar_is_utf16_high_surrogate(code_unit)) {                        \
        size_t nwritten =                                                      \
            scalar_casefold_character_utf16##endianness##_bmp(code_unit, out); \
        if (nwritten == 0) {                                                   \
          *out++ = input[p];                                                   \
          *out++ = input[p + 1];                                               \
        } else {                                                               \
          out += nwritten;                                                     \
        }                                                                      \
        p += 2;                                                                \
      } else {                                                                 \
        uint16_t low_surrogate =                                               \
            scalar_read_uint16##endianness(input + p + 2);                     \
        uint32_t code_point = (((uint32_t)(code_unit - 0xD800) << 10) |        \
                               ((uint32_t)(low_surrogate - 0xDC00))) +         \
                              0x10000;                                         \
        size_t nwritten =                                                      \
            scalar_casefold_character_utf16##endianness##_supplementary(       \
                code_point, out);                                              \
        if (nwritten == 0) {                                                   \
          *out++ = input[p];                                                   \
          *out++ = input[p + 1];                                               \
          *out++ = input[p + 2];                                               \
          *out++ = input[p + 3];                                               \
        } else {                                                               \
          out += nwritten;                                                     \
        }                                                                      \
        p += 4;                                                                \
      }                                                                        \
    }                                                                          \
                                                                               \
    return out - start;                                                        \
  }                                                                            \
                                                                               \
  static bool scalar_casefold_length_utf16##endianness##_bmp(                  \
      uint16_t c, size_t *out_length) {                                        \
    uint16_t shifted = c >> 6;                                                 \
    uint16_t masked = c & 63;                                                  \
    uint16_t index = UNIDATA_UTF16_CASEFOLD_CHECK_TRIE_INDEX[shifted];         \
    uint8_t value = UNIDATA_UTF16_CASEFOLD_CHECK_TRIE_DATA[index + masked];    \
    *out_length += value & 0x7F;                                               \
    return !(value >> 7);                                                      \
  }                                                                            \
                                                                               \
  static bool scalar_casefold_length_utf16##endianness##_supplementary(        \
      uint32_t c, size_t *out_length) {                                        \
    uint32_t salt_hash =                                                       \
        scalar_phash(c, 0, sizeof(UNIDATA_CASEFOLD_KV) / sizeof(uint32_t));    \
    uint32_t salt = UNIDATA_CASEFOLD_SALT[salt_hash];                          \
    uint32_t key_hash =                                                        \
        scalar_phash(c, salt, sizeof(UNIDATA_CASEFOLD_KV) / sizeof(uint32_t)); \
    uint32_t k = UNIDATA_CASEFOLD_KV[key_hash][1];                             \
    uint32_t casefold = UNIDATA_CASEFOLD_KV[key_hash][0];                      \
    if (k == c) {                                                              \
      *out_length += scalar_code_point_size_utf8(casefold);                    \
      return false;                                                            \
    }                                                                          \
    *out_length += 4;                                                          \
    return true;                                                               \
  }                                                                            \
                                                                               \
  bool scalar_casefold_utf16##endianness##_check(                              \
      const uint8_t *input, size_t length, size_t *out_length) {               \
    *out_length = 0;                                                           \
    bool is_qc = true;                                                         \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint16_t code_unit = scalar_read_uint16##endianness(input + p);          \
      if (!scalar_is_utf16_high_surrogate(code_unit)) {                        \
        is_qc &= scalar_casefold_length_utf16##endianness##_bmp(code_unit,     \
                                                                out_length);   \
        p += 2;                                                                \
      } else {                                                                 \
        uint16_t low_surrogate =                                               \
            scalar_read_uint16##endianness(input + p + 2);                     \
        uint32_t code_point = (((uint32_t)(code_unit - 0xD800) << 10) |        \
                               ((uint32_t)(low_surrogate - 0xDC00))) +         \
                              0x10000;                                         \
        is_qc &= scalar_casefold_length_utf16##endianness##_supplementary(     \
            code_point, out_length);                                           \
        p += 4;                                                                \
      }                                                                        \
    }                                                                          \
    return is_qc;                                                              \
  }

SCALAR_UTF16_IMPLEMENTATION(le, false);
SCALAR_UTF16_IMPLEMENTATION(be, true);

#undef SCALAR_UTF16_IMPLEMENTATION
