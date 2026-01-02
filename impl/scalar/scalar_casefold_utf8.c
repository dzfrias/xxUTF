#include "impl/scalar.h"
#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Casefold a character in the BMP. Returns zero if no casefold is found for the
// given character.
static size_t scalar_casefold_character_utf8_bmp(uint16_t c, uint8_t *out) {
  uint16_t shifted = c >> 6;
  uint16_t masked = c & 63;
  uint16_t index = NORMDATA_UTF8_CASEFOLD_TRIE_INDEX[shifted];
  uint32_t value = NORMDATA_UTF8_CASEFOLD_TRIE_DATA[index + masked];
  if (value == 0) {
    return 0;
  }
  // Check if we have a common case folding
  if ((value & 0x80) == 0) {
    int32_t delta = (int32_t)value >> 8;
    int32_t mapping = (uint32_t)c - delta;
    assert(mapping > 0);
    assert(mapping <= 0xFFFF);
    return scalar_write_code_point_utf8((uint32_t)mapping, out);
  }
  // In this case, we have a full case folding
  uint8_t length = value & 0x7F;
  size_t offset = value >> 8;
  for (size_t k = 0; k < length; k++) {
    out[k] = NORMDATA_UTF8_CASEFOLD_DATA[offset + k];
  }
  return length;
}

// Casefold a character in the supplementary plane. Returns zero if no casefold
// is found for the given character.
static size_t scalar_casefold_character_utf8_supplementary(uint32_t c,
                                                           uint8_t *out) {
  uint32_t salt_hash = scalar_phash(c, 0, NORMDATA_CASEFOLD_TABLE_SIZE);
  uint32_t salt = NORMDATA_CASEFOLD_SALT[salt_hash];
  uint32_t key_hash = scalar_phash(c, salt, NORMDATA_CASEFOLD_TABLE_SIZE);
  uint32_t k = NORMDATA_CASEFOLD_KV[key_hash][1];
  uint32_t casefold = NORMDATA_CASEFOLD_KV[key_hash][0];
  if (k == c) {
    return scalar_write_code_point_utf8(casefold, out);
  }
  return 0;
}

size_t scalar_casefold_utf8(const uint8_t *input, size_t length, uint8_t *out) {
  uint8_t *start = out;

  size_t p = 0;
  while (p < length) {
    uint8_t leading = input[p];

    if (leading < 0b10000000) {
      if (leading >= 'A' && leading <= 'Z') {
        *out++ = leading + 32;
      } else {
        *out++ = leading;
      }
      p++;
    } else if ((leading & 0b11100000) == 0b11000000) {
      uint16_t code_point =
          (leading & 0b00011111) << 6 | (input[p + 1] & 0b00111111);
      size_t nwritten = scalar_casefold_character_utf8_bmp(code_point, out);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = input[p + 1];
      } else {
        out += nwritten;
      }
      p += 2;
    } else if ((leading & 0b11110000) == 0b11100000) {
      uint16_t code_point = (leading & 0b00001111) << 12 |
                            (input[p + 1] & 0b00111111) << 6 |
                            (input[p + 2] & 0b00111111);
      size_t nwritten = scalar_casefold_character_utf8_bmp(code_point, out);
      if (nwritten == 0) {
        *out++ = leading;
        *out++ = input[p + 1];
        *out++ = input[p + 2];
      } else {
        out += nwritten;
      }
      p += 3;
    } else if ((leading & 0b11111000) == 0b11110000) {
      uint32_t code_point =
          (leading & 0b00000111) << 18 | (input[p + 1] & 0b00111111) << 12 |
          (input[p + 2] & 0b00111111) << 6 | (input[p + 3] & 0b00111111);
      size_t nwritten =
          scalar_casefold_character_utf8_supplementary(code_point, out);
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
  }

  return out - start;
}
