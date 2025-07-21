#include "impl/scalar.h"
#include "normdata.h"
#include <stdint.h>
#include <stdio.h>

static uint32_t phash(uint32_t key, uint32_t salt, uint64_t size) {
  uint32_t salt_key = key + salt;
  uint32_t y1 = salt_key * 2654435769;
  uint32_t y2 = key * 0x31415926;
  uint32_t y = y1 ^ y2;
  uint64_t mh = (uint64_t)y * size;
  return mh >> 32;
}

// Decompose a code point and write it into the output buffer. Returns the
// number of bytes written, or zero if the provided code point doesn't have a
// decomposition.
size_t decompose(uint32_t code_point, char *out) {
  static const uint32_t SALT_SIZE = sizeof(DECOMPOSED_SALT) / 2;

  char *start = out;
  uint32_t salt_hash = phash(code_point, 0, SALT_SIZE);
  uint32_t salt = DECOMPOSED_SALT[salt_hash];
  uint32_t key_hash = phash(code_point, salt, SALT_SIZE);
  Entry kv = DECOMPOSED_KV[key_hash];
  if (kv.k == code_point) {
    uint8_t const *bytes = &DECOMPOSED_CHARS[kv.v1];
    for (uint16_t k = 0; k < kv.v2; k++) {
      *out++ = bytes[k];
    }
  }

  return out - start;
}

size_t normalize_utf8_nfd_scalar(char const *input, size_t length, char *out) {
  uint8_t const *data = (uint8_t const *)input;
  char *start = out;

  size_t p = 0;
  while (p < length) {
    uint8_t leading = data[p];

    if (leading < 0b10000000) {
      // ASCII, no need to do a lookup
      *out++ = leading;
      p++;
    } else if ((leading & 0b11100000) == 0b11000000) {
      // Two byte UTF-8, combine with next. Note that we do no error handling.
      uint32_t code_point =
          (leading & 0b00011111) << 6 | (data[p + 1] & 0b00111111);
      size_t nwritten = decompose(code_point, out);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = data[p + 1];
      } else {
        out += nwritten;
      }
      p += 2;
    } else if ((leading & 0b11110000) == 0b11100000) {
      uint32_t code_point = (leading & 0b00001111) << 12 |
                            (data[p + 1] & 0b00111111) << 6 |
                            (data[p + 2] & 0b00111111);
      size_t nwritten = decompose(code_point, out);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = data[p + 1];
        *out++ = data[p + 2];
      } else {
        out += nwritten;
      }
      p += 3;
    } else if ((leading & 0b11111000) == 0b11110000) {
      uint32_t code_point =
          (leading & 0b00000111) << 18 | (data[p + 1] & 0b00111111) << 12 |
          (data[p + 2] & 0b00111111) << 6 | (data[p + 3] & 0b00111111);
      size_t nwritten = decompose(code_point, out);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = data[p + 1];
        *out++ = data[p + 2];
        *out++ = data[p + 3];
      } else {
        out += nwritten;
      }
      p += 4;
    }
  }

  return out - start;
}
