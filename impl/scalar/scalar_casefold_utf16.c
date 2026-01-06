#include "impl/scalar.h"
#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCALAR_UTF16_IMPLEMENTATION(endianness, is_big_endian)                 \
  static size_t scalar_casefold_character_utf16##endianness##_bmp(             \
      uint16_t c, uint8_t *out) {                                              \
    uint16_t shifted = c >> 6;                                                 \
    uint16_t masked = c & 63;                                                  \
    uint16_t index = NORMDATA_UTF16_CASEFOLD_TRIE_INDEX[shifted];              \
    uint16_t value = NORMDATA_UTF16_CASEFOLD_TRIE_DATA[index + masked];        \
    if (value == 0) {                                                          \
      return 0;                                                                \
    }                                                                          \
    uint8_t length = value >> 12;                                              \
    size_t offset = value & 0xFFF;                                             \
    const uint8_t *bytes = &NORMDATA_UTF16_CASEFOLD_DATA[offset];              \
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
    uint32_t salt_hash = scalar_phash(c, 0, NORMDATA_CASEFOLD_TABLE_SIZE);     \
    uint32_t salt = NORMDATA_CASEFOLD_SALT[salt_hash];                         \
    uint32_t key_hash = scalar_phash(c, salt, NORMDATA_CASEFOLD_TABLE_SIZE);   \
    uint32_t k = NORMDATA_CASEFOLD_KV[key_hash][1];                            \
    uint32_t casefold = NORMDATA_CASEFOLD_KV[key_hash][0];                     \
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
  static uint8_t scalar_casefold_length_utf16##endianness##_bmp(uint16_t c) {  \
    uint16_t shifted = c >> 6;                                                 \
    uint16_t masked = c & 63;                                                  \
    uint16_t index = NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[shifted];       \
    uint8_t value = NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[index + masked];  \
    return value;                                                              \
  }                                                                            \
                                                                               \
  static uint8_t scalar_casefold_length_utf16##endianness##_supplementary(     \
      uint32_t c) {                                                            \
    uint32_t salt_hash = scalar_phash(c, 0, NORMDATA_CASEFOLD_TABLE_SIZE);     \
    uint32_t salt = NORMDATA_CASEFOLD_SALT[salt_hash];                         \
    uint32_t key_hash = scalar_phash(c, salt, NORMDATA_CASEFOLD_TABLE_SIZE);   \
    uint32_t k = NORMDATA_CASEFOLD_KV[key_hash][1];                            \
    uint32_t casefold = NORMDATA_CASEFOLD_KV[key_hash][0];                     \
    if (k == c) {                                                              \
      return scalar_code_point_size_utf16(casefold);                           \
    }                                                                          \
    return 4;                                                                  \
  }                                                                            \
                                                                               \
  size_t scalar_casefold_utf16##endianness##_length(const uint8_t *input,      \
                                                    size_t length) {           \
    size_t out_length = 0;                                                     \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint16_t code_unit = scalar_read_uint16##endianness(input + p);          \
      if (!scalar_is_utf16_high_surrogate(code_unit)) {                        \
        out_length +=                                                          \
            scalar_casefold_length_utf16##endianness##_bmp(code_unit);         \
        p += 2;                                                                \
      } else {                                                                 \
        uint16_t low_surrogate =                                               \
            scalar_read_uint16##endianness(input + p + 2);                     \
        uint32_t code_point = (((uint32_t)(code_unit - 0xD800) << 10) |        \
                               ((uint32_t)(low_surrogate - 0xDC00))) +         \
                              0x10000;                                         \
        out_length +=                                                          \
            scalar_casefold_length_utf16##endianness##_supplementary(          \
                code_point);                                                   \
        p += 4;                                                                \
      }                                                                        \
    }                                                                          \
    return out_length;                                                         \
  }

SCALAR_UTF16_IMPLEMENTATION(le, false);
SCALAR_UTF16_IMPLEMENTATION(be, true);

#undef SCALAR_UTF16_IMPLEMENTATION
