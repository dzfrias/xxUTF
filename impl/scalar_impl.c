#ifndef DECOMP_SUFFIX
#error "DECOMP_SUFFIX must be defined"
#endif

#ifndef DECOMP_TABLE_NAME
#error "DECOMP_TABLE_NAME must be defined"
#endif

#include "impl/scalar_common.h"
#include "normdata.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define _CONCAT2(a, b) a##b
#define CONCAT2(a, b) _CONCAT2(a, b)
#define _CONCAT3(a, b, c) a##b##c
#define CONCAT3(a, b, c) _CONCAT3(a, b, c)

// Decompose a code point and write it into the output buffer. Returns the
// number of bytes written, or zero if the provided code point doesn't have a
// decomposition.
//
// Note that this does not handle Hangul code points.
size_t CONCAT2(scalar_decompose_, DECOMP_SUFFIX)(uint32_t code_point,
                                                 uint8_t *out, bool *is_cc) {
  uint8_t *start = out;
  uint32_t salt_hash = scalar_phash(
      code_point, 0, CONCAT3(NORMDATA_, DECOMP_TABLE_NAME, _TABLE_SIZE));
  uint32_t salt = CONCAT3(NORMDATA_, DECOMP_TABLE_NAME, _SALT)[salt_hash];
  uint32_t key_hash = scalar_phash(
      code_point, salt, CONCAT3(NORMDATA_, DECOMP_TABLE_NAME, _TABLE_SIZE));
  NormdataTableEntry kv = CONCAT3(NORMDATA_, DECOMP_TABLE_NAME, _KV)[key_hash];
  if (kv.k == code_point) {
    uint8_t const *bytes =
        &CONCAT3(NORMDATA_, DECOMP_TABLE_NAME, _CHARS)[kv.offset];
    for (uint8_t k = 0; k < kv.len; k++) {
      *out++ = bytes[k];
    }
    *is_cc = kv.ccc > 0;
  } else {
    *is_cc = false;
  }

  return out - start;
}

size_t CONCAT3(scalar_normalize_utf8_, DECOMP_SUFFIX,
               _with_context)(uint8_t const *input, size_t length, uint8_t *out,
                              bool *end_is_cc) {
  uint8_t *start = out;

  bool last_is_cc = *end_is_cc;
  size_t p = 0;
  while (p < length) {
    uint8_t leading = input[p];

    bool is_cc = false;
    uint8_t *c_start = out;
    if (leading < 0b10000000) {
      // ASCII, no need to do a lookup
      *out++ = leading;
      p++;
    } else if ((leading & 0b11100000) == 0b11000000) {
      // Two byte UTF-8, combine with next. Note that we do no error handling.
      uint32_t code_point =
          (leading & 0b00011111) << 6 | (input[p + 1] & 0b00111111);
      size_t nwritten =
          CONCAT2(scalar_decompose_, DECOMP_SUFFIX)(code_point, out, &is_cc);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = input[p + 1];
      } else {
        out += nwritten;
      }
      p += 2;
    } else if ((leading & 0b11110000) == 0b11100000) {
      uint32_t code_point = (leading & 0b00001111) << 12 |
                            (input[p + 1] & 0b00111111) << 6 |
                            (input[p + 2] & 0b00111111);
      // The Hangul block exists entirely within the three-byte UTF-8 range.
      if (scalar_is_hangul(code_point)) {
        is_cc = false;
        out += scalar_decompose_hangul(code_point, out);
      } else {
        size_t nwritten =
            CONCAT2(scalar_decompose_, DECOMP_SUFFIX)(code_point, out, &is_cc);
        if (nwritten == 0) {
          *out++ = leading;
          *out++ = input[p + 1];
          *out++ = input[p + 2];
        } else {
          out += nwritten;
        }
      }
      p += 3;
    } else if ((leading & 0b11111000) == 0b11110000) {
      uint32_t code_point =
          (leading & 0b00000111) << 18 | (input[p + 1] & 0b00111111) << 12 |
          (input[p + 2] & 0b00111111) << 6 | (input[p + 3] & 0b00111111);
      size_t nwritten =
          CONCAT2(scalar_decompose_, DECOMP_SUFFIX)(code_point, out, &is_cc);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = input[p + 1];
        *out++ = input[p + 2];
        *out++ = input[p + 3];
      } else {
        out += nwritten;
      }
      p += 4;
    }

    // If the last character was a non-starter and the current character is a
    // starter, we need to try to reorder the combining characters
    if (last_is_cc && !is_cc) {
      scalar_sort_characters(c_start - 1);
    }
    last_is_cc = is_cc;
  }

  // Try to sort at EOF
  if (last_is_cc) {
    scalar_sort_characters(out - 1);
  }

  *end_is_cc = last_is_cc;

  return out - start;
}

size_t CONCAT2(scalar_normalize_utf8_, DECOMP_SUFFIX)(const uint8_t *input,
                                                      size_t length,
                                                      uint8_t *out) {
  bool end_is_cc = false;
  return CONCAT3(scalar_normalize_utf8_, DECOMP_SUFFIX,
                 _with_context)(input, length, out, &end_is_cc);
}

#undef _CONCAT
#undef CONCAT
#undef _CONCAT3
#undef CONCAT3
