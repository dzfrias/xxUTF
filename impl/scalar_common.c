#include "impl/scalar_common.h"
#include "normdata.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Hash using the perfect hash function for the given key and salt. See
// gen/gen.py for the reference implementation of the perfect hash function.
uint32_t scalar_phash(uint32_t key, uint32_t salt, uint64_t size) {
  uint32_t salt_key = key + salt;
  uint32_t y1 = salt_key * 2654435769;
  uint32_t y2 = key * 0x31415926;
  uint32_t y = y1 ^ y2;
  uint64_t mh = (uint64_t)y * size;
  return mh >> 32;
}

// Write a code point into the output buffer as UTF-8 bytes. Returns the number
// of bytes written.
size_t scalar_write_code_point(uint32_t codepoint, uint8_t *utf8_bytes) {
  if (codepoint <= 0x7F) {
    utf8_bytes[0] = (uint8_t)(codepoint & 0xFF);
    return 1;
  } else if (codepoint <= 0x7FF) {
    utf8_bytes[0] = 0xC0 | (codepoint >> 6);
    utf8_bytes[1] = 0x80 | (codepoint & 0x3F);
    return 2;
  } else if (codepoint <= 0xFFFF) {
    utf8_bytes[0] = 0xE0 | (codepoint >> 12);
    utf8_bytes[1] = 0x80 | ((codepoint >> 6) & 0x3F);
    utf8_bytes[2] = 0x80 | (codepoint & 0x3F);
    return 3;
  } else if (codepoint <= 0x10FFFF) {
    utf8_bytes[0] = 0xF0 | (codepoint >> 18);
    utf8_bytes[1] = 0x80 | ((codepoint >> 12) & 0x3F);
    utf8_bytes[2] = 0x80 | ((codepoint >> 6) & 0x3F);
    utf8_bytes[3] = 0x80 | (codepoint & 0x3F);
    return 4;
  } else {
    // Code point is too large, but we don't handle errors
    return 0;
  }
}

size_t scalar_code_point_size(uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    return 1;
  } else if (codepoint <= 0x7FF) {
    return 2;
  } else if (codepoint <= 0xFFFF) {
    return 3;
  } else if (codepoint <= 0x10FFFF) {
    return 4;
  } else {
    // Code point is too large, but we don't handle errors
    return 0;
  }
}

// Check if a code point is in the Hangul block.
bool scalar_is_hangul(uint32_t code_point) {
  return code_point >= NORMDATA_S_BASE &&
         code_point < NORMDATA_S_BASE + NORMDATA_S_COUNT;
}

// Hangul code points can be decomposed into Hangul syllables algorithmically.
size_t scalar_decompose_hangul(uint32_t code_point, uint8_t *out) {
  uint32_t s_index = code_point - NORMDATA_S_BASE;
  uint32_t l_index = s_index / NORMDATA_N_COUNT;
  uint32_t v_index = (s_index % NORMDATA_N_COUNT) / NORMDATA_T_COUNT;
  uint32_t t_index = s_index % NORMDATA_T_COUNT;

  size_t nwritten = 0;
  nwritten += scalar_write_code_point(NORMDATA_L_BASE + l_index, out);
  nwritten +=
      scalar_write_code_point(NORMDATA_V_BASE + v_index, out + nwritten);
  if (t_index > 0) {
    nwritten +=
        scalar_write_code_point(NORMDATA_T_BASE + t_index, out + nwritten);
  }
  return nwritten;
}

// Look up the canonical combining class (CCC) for a code point.
//
// See: https://www.unicode.org/reports/tr44/#Canonical_Combining_Class_Values
uint8_t scalar_lookup_ccc(uint32_t code_point) {
  uint32_t salt_hash = scalar_phash(code_point, 0, NORMDATA_NFD_TABLE_SIZE);
  uint32_t salt = NORMDATA_NFD_SALT[salt_hash];
  uint32_t key_hash = scalar_phash(code_point, salt, NORMDATA_NFD_TABLE_SIZE);
  NormdataTableEntry kv = NORMDATA_NFD_KV[key_hash];
  if (kv.k == code_point) {
    return kv.ccc;
  }
  return 0;
}

// Parse a UTF-8 code point from the input buffer. The size of the code point is
// written to the `size` pointer.
uint32_t scalar_parse_code_point(uint8_t const *input, uint8_t *size) {
  uint8_t leading = *input;
  if (leading < 0b10000000) {
    *size = 1;
    return leading;
  } else if ((leading & 0b11100000) == 0b11000000) {
    *size = 2;
    return (leading & 0b00011111) << 6 | (input[1] & 0b00111111);
  } else if ((leading & 0b11110000) == 0b11100000) {
    *size = 3;
    return (leading & 0b00001111) << 12 | (input[1] & 0b00111111) << 6 |
           (input[2] & 0b00111111);
  } else if ((leading & 0b11111000) == 0b11110000) {
    *size = 4;
    return (leading & 0b00000111) << 18 | (input[1] & 0b00111111) << 12 |
           (input[2] & 0b00111111) << 6 | (input[3] & 0b00111111);
  } else {
    *size = 0;
    // This should be an error, but we don't handle errors
    return 0;
  }
}

// Reverse a subsection of an array.
static void scalar_reverse(uint8_t *array, size_t start, size_t end) {
  while (start < end) {
    uint8_t tmp = array[start];
    array[start] = array[end];
    array[end] = tmp;
    start++;
    end--;
  }
}

// Rotate a subsection of an array to the right by k positions.
static void scalar_rotate(uint8_t *array, size_t size, size_t k) {
  scalar_reverse(array, 0, size - 1);
  scalar_reverse(array, 0, k - 1);
  scalar_reverse(array, k, size - 1);
}

// Check if a given byte is the leading byte of a UTF-8 code point
bool scalar_is_leading_utf8_byte(uint8_t b) {
  return (b & 0b11000000) != 0b10000000;
}

// Sort combining characters in-place (implementation of the canonical ordering
// algorithm). This is done by walking backwards from the end of the buffer
// until a starter character is found and sorting the combining characters from
// there.
//
// TODO: undefined behavior when we can't find a starter when walking backwards?
//       We don't know when to stop.
void scalar_sort_characters(uint8_t *out) {
  uint8_t *start = out;

  // We need to walk backwards until we find a starter character.
  uint8_t last_ccc = 255;
  bool needs_sort = false;
  while (true) {
    while (!scalar_is_leading_utf8_byte(*out)) {
      out--;
    }
    uint8_t size;
    uint32_t code_point = scalar_parse_code_point(out, &size);
    uint8_t ccc = scalar_lookup_ccc(code_point);
    if (last_ccc < ccc) {
      needs_sort = true;
    }
    // If we found a starter, then we're done
    if (ccc == 0) {
      break;
    }
    out--;
    last_ccc = ccc;
  }

  // Fast path if the combining characters are already sorted
  if (!needs_sort) {
    return;
  }

  // We do bubble sort on starting at the starter code point, up until the next
  // starter. The implementation supports sorting any number of combining
  // characters with no memory allocation. Sorting is thus done entirely
  // in-place and still while all code points are in UTF-8-encoded form. In
  // practice, n will be small.
  size_t n = start - out;
  // This loop will run until we detect no more swaps, in which case we will
  // have sorted the buffer.
  while (true) {
    bool did_swap = false;
    uint8_t last_size;
    for (size_t j = 0; j < n; j += last_size) {
      uint8_t size1;
      uint8_t size2;
      // TODO: odds are we can fit this information into a small cache from the
      //       initial backwards loop
      uint32_t c1 = scalar_parse_code_point(out + j, &size1);
      // Going past the buffer is also a stop condition
      if (j + size1 >= n) {
        break;
      }
      uint32_t c2 = scalar_parse_code_point(out + j + size1, &size2);
      uint8_t ccc1 = scalar_lookup_ccc(c1);
      uint8_t ccc2 = scalar_lookup_ccc(c2);
      last_size = size1;
      if (ccc1 > ccc2) {
        // Swapping two adjacent, variably sized UTF-8 encoded code points can
        // be done with a right rotation by the size of the right code point.
        scalar_rotate(out + j, size1 + size2, size2);
        last_size = size2;
        did_swap = true;
      }
    }
    if (!did_swap) {
      break;
    }
  }
}
