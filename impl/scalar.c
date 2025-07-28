#include "impl/scalar.h"
#include "normdata.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t scalar_phash(uint32_t key, uint32_t salt, uint64_t size) {
  uint32_t salt_key = key + salt;
  uint32_t y1 = salt_key * 2654435769;
  uint32_t y2 = key * 0x31415926;
  uint32_t y = y1 ^ y2;
  uint64_t mh = (uint64_t)y * size;
  return mh >> 32;
}

size_t scalar_write_codepoint(uint32_t codepoint, uint8_t *utf8_bytes) {
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

// Check if a code point is in the Hangul block.
bool scalar_is_hangul(uint32_t code_point) {
  return code_point >= NORMDATA_S_BASE &&
         code_point < NORMDATA_S_BASE + NORMDATA_S_COUNT;
}

// Hangul code points can be decomposed into Hangul syllables algorithmically.
size_t scalar_decompose_hangul(uint32_t code_point, char *out) {
  uint32_t s_index = code_point - NORMDATA_S_BASE;
  uint32_t l_index = s_index / NORMDATA_N_COUNT;
  uint32_t v_index = (s_index % NORMDATA_N_COUNT) / NORMDATA_T_COUNT;
  uint32_t t_index = s_index % NORMDATA_T_COUNT;

  uint8_t *data = (uint8_t *)out;
  size_t nwritten = 0;
  nwritten += scalar_write_codepoint(NORMDATA_L_BASE + l_index, data);
  nwritten +=
      scalar_write_codepoint(NORMDATA_V_BASE + v_index, data + nwritten);
  if (t_index > 0) {
    nwritten +=
        scalar_write_codepoint(NORMDATA_T_BASE + t_index, data + nwritten);
  }
  return nwritten;
}

// Decompose a code point and write it into the output buffer. Returns the
// number of bytes written, or zero if the provided code point doesn't have a
// decomposition.
//
// Note that this does not handle Hangul code points.
static size_t scalar_decompose(uint32_t code_point, char *out, bool *is_cc) {
  char *start = out;
  uint32_t salt_hash =
      scalar_phash(code_point, 0, NORMDATA_DECOMPOSED_SALT_SIZE);
  uint32_t salt = NORMDATA_DECOMPOSED_SALT[salt_hash];
  uint32_t key_hash =
      scalar_phash(code_point, salt, NORMDATA_DECOMPOSED_SALT_SIZE);
  NormdataEntry kv = NORMDATA_DECOMPOSED_KV[key_hash];
  if (kv.k == code_point) {
    uint8_t const *bytes = &NORMDATA_DECOMPOSED_CHARS[kv.offset];
    for (uint8_t k = 0; k < kv.len; k++) {
      *out++ = bytes[k];
    }
    *is_cc = kv.ccc > 0;
  } else {
    *is_cc = false;
  }

  return out - start;
}

static uint8_t scalar_lookup_ccc(uint32_t code_point) {
  static const uint32_t SALT_SIZE = sizeof(NORMDATA_DECOMPOSED_SALT) / 2;

  uint32_t salt_hash = scalar_phash(code_point, 0, SALT_SIZE);
  uint32_t salt = NORMDATA_DECOMPOSED_SALT[salt_hash];
  uint32_t key_hash = scalar_phash(code_point, salt, SALT_SIZE);
  NormdataEntry kv = NORMDATA_DECOMPOSED_KV[key_hash];
  if (kv.k == code_point) {
    return kv.ccc;
  }
  return 0;
}

static uint32_t scalar_parse_code_point(uint8_t const *input, uint8_t *size) {
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

static void scalar_reverse(uint8_t *array, size_t size, size_t start,
                           size_t end) {
  while (start < end) {
    uint8_t tmp = array[start];
    array[start] = array[end];
    array[end] = tmp;
    start++;
    end--;
  }
}

static void scalar_rotate(uint8_t *array, size_t size, size_t k) {
  scalar_reverse(array, size, 0, size - 1);
  scalar_reverse(array, size, 0, k - 1);
  scalar_reverse(array, size, k, size - 1);
}

static void scalar_sort_characters(char *out) {
  uint8_t *start = (uint8_t *)out;
  uint8_t *data = (uint8_t *)out;

  // We need to walk backwards until we find a starter character.
  uint8_t last_ccc = 255;
  bool needs_sort = false;
  while (true) {
    while ((*data & 0b11000000) == 0b10000000) {
      data--;
    }
    uint8_t size;
    uint32_t code_point = scalar_parse_code_point(data, &size);
    uint8_t ccc = scalar_lookup_ccc(code_point);
    if (last_ccc < ccc) {
      needs_sort = true;
    }
    // If we found a starter, then we're done
    if (ccc == 0) {
      break;
    }
    data--;
    last_ccc = ccc;
  }

  if (!needs_sort) {
    return;
  }

  // We do bubble sort on starting at the starter code point, up until the next
  // starter. The implementation supports sorting any number of combining
  // characters with no memory allocation. Sorting is thus done entirely
  // in-place and still while all code points are in UTF-8-encoded form. In
  // practice, n will be small.
  size_t n = start - data;
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
      uint32_t c1 = scalar_parse_code_point(data + j, &size1);
      // Going past the buffer is also a stop condition
      if (j + size1 >= n) {
        break;
      }
      uint32_t c2 = scalar_parse_code_point(data + j + size1, &size2);
      uint8_t ccc1 = scalar_lookup_ccc(c1);
      uint8_t ccc2 = scalar_lookup_ccc(c2);
      last_size = size1;
      if (ccc1 > ccc2) {
        // Swapping two adjacent, variably sized UTF-8 encoded code points can
        // be done with a right rotation by the size of the right code point.
        scalar_rotate(data + j, size1 + size2, size2);
        last_size = size2;
        did_swap = true;
      }
    }
    if (!did_swap) {
      break;
    }
  }
}

size_t scalar_normalize_utf8_nfd(char const *input, size_t length, char *out) {
  uint8_t const *data = (uint8_t const *)input;
  char *start = out;

  bool last_is_cc = false;
  size_t p = 0;
  while (p < length) {
    uint8_t leading = data[p];

    bool is_cc = false;
    char *c_start = out;
    if (leading < 0b10000000) {
      // ASCII, no need to do a lookup
      *out++ = leading;
      p++;
    } else if ((leading & 0b11100000) == 0b11000000) {
      // Two byte UTF-8, combine with next. Note that we do no error handling.
      uint32_t code_point =
          (leading & 0b00011111) << 6 | (data[p + 1] & 0b00111111);
      size_t nwritten = scalar_decompose(code_point, out, &is_cc);
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
      // The Hangul block exists entirely within the three-byte UTF-8 range.
      if (scalar_is_hangul(code_point)) {
        is_cc = false;
        out += scalar_decompose_hangul(code_point, out);
      } else {
        size_t nwritten = scalar_decompose(code_point, out, &is_cc);
        if (nwritten == 0) {
          *out++ = leading;
          *out++ = data[p + 1];
          *out++ = data[p + 2];
        } else {
          out += nwritten;
        }
      }
      p += 3;
    } else if ((leading & 0b11111000) == 0b11110000) {
      uint32_t code_point =
          (leading & 0b00000111) << 18 | (data[p + 1] & 0b00111111) << 12 |
          (data[p + 2] & 0b00111111) << 6 | (data[p + 3] & 0b00111111);
      size_t nwritten = scalar_decompose(code_point, out, &is_cc);
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

  return out - start;
}
