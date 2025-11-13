#include "impl/scalar.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Hash using the perfect hash function for the given key and salt. See
// gen/gen.py for the reference implementation of the perfect hash function.
static uint32_t scalar_phash(uint32_t key, uint32_t salt, uint64_t size) {
  uint32_t salt_key = key + salt;
  uint32_t y1 = salt_key * 2654435769;
  uint32_t y2 = key * 0x31415926;
  uint32_t y = y1 ^ y2;
  uint64_t mh = (uint64_t)y * size;
  return mh >> 32;
}

// Write a code point into the output buffer as UTF-8 bytes. Returns the number
// of bytes written.
static size_t scalar_write_code_point(uint32_t codepoint, uint8_t *utf8_bytes) {
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

static size_t scalar_code_point_size(uint32_t codepoint) {
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
static bool scalar_is_hangul(uint32_t code_point) {
  return code_point >= NORMDATA_S_BASE &&
         code_point < NORMDATA_S_BASE + NORMDATA_S_COUNT;
}

// Hangul code points can be decomposed into Hangul syllables algorithmically.
static size_t scalar_decompose_hangul(uint32_t code_point, uint8_t *out) {
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

// Decompose a code point and write it into the output buffer. Returns the
// number of bytes written, or zero if the provided code point doesn't have a
// decomposition.
//
// Note that this does not handle Hangul code points.
size_t scalar_decompose(uint32_t code_point, uint8_t *out, bool *is_cc) {
  uint8_t *start = out;
  uint32_t salt_hash =
      scalar_phash(code_point, 0, NORMDATA_DECOMPOSED_TABLE_SIZE);
  uint32_t salt = NORMDATA_DECOMPOSED_SALT[salt_hash];
  uint32_t key_hash =
      scalar_phash(code_point, salt, NORMDATA_DECOMPOSED_TABLE_SIZE);
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

// Try to compose two BMP code points into a single code point. Returns the
// composed code point if the composition is valid, or zero if the composition
// is not valid.
static uint32_t scalar_try_compose_bmp(uint16_t c1, uint16_t c2) {
  if (c1 >= NORMDATA_L_BASE && c1 < NORMDATA_L_BASE + NORMDATA_L_COUNT &&
      c2 >= NORMDATA_V_BASE && c2 < NORMDATA_V_BASE + NORMDATA_V_COUNT) {
    uint32_t l_index = c1 - NORMDATA_L_BASE;
    uint32_t v_index = c2 - NORMDATA_V_BASE;
    uint32_t lv_index = l_index * NORMDATA_N_COUNT + v_index * NORMDATA_T_COUNT;
    return NORMDATA_S_BASE + lv_index;
  }
  if (c1 >= NORMDATA_S_BASE && c1 < NORMDATA_S_BASE + NORMDATA_S_COUNT &&
      (c1 - NORMDATA_S_BASE) % NORMDATA_T_COUNT == 0 && c2 >= NORMDATA_T_BASE &&
      c2 < NORMDATA_T_BASE + NORMDATA_T_COUNT) {
    return c1 + (c2 - NORMDATA_T_BASE);
  }

  uint32_t wide = c1;
  uint32_t key = (wide << 16) | c2;
  uint32_t salt_hash = scalar_phash(key, 0, NORMDATA_COMPOSED_TABLE_SIZE);
  uint32_t salt = NORMDATA_COMPOSITION_SALT[salt_hash];
  uint32_t key_hash = scalar_phash(key, salt, NORMDATA_COMPOSED_TABLE_SIZE);
  uint32_t k = NORMDATA_COMPOSITION_KV[key_hash][1];
  uint32_t comp = NORMDATA_COMPOSITION_KV[key_hash][0];
  if (k == key) {
    // The composition is valid, return the composed code point
    return comp;
  } else {
    return 0;
  }
}

// Look up the canonical combining class (CCC) for a code point.
//
// See: https://www.unicode.org/reports/tr44/#Canonical_Combining_Class_Values
static uint8_t scalar_lookup_ccc(uint32_t code_point) {
  uint32_t salt_hash =
      scalar_phash(code_point, 0, NORMDATA_DECOMPOSED_TABLE_SIZE);
  uint32_t salt = NORMDATA_DECOMPOSED_SALT[salt_hash];
  uint32_t key_hash =
      scalar_phash(code_point, salt, NORMDATA_DECOMPOSED_TABLE_SIZE);
  NormdataEntry kv = NORMDATA_DECOMPOSED_KV[key_hash];
  if (kv.k == code_point) {
    return kv.ccc;
  }
  return 0;
}

// Parse a UTF-8 code point from the input buffer. The size of the code point is
// written to the `size` pointer.
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
static bool scalar_is_leading_utf8_byte(uint8_t b) {
  return (b & 0b11000000) != 0b10000000;
}

// Sort combining characters in-place (implementation of the canonical ordering
// algorithm). This is done by walking backwards from the end of the buffer
// until a starter character is found and sorting the combining characters from
// there.
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

size_t scalar_normalize_utf8_nfd_with_context(uint8_t const *input,
                                              size_t length, uint8_t *out,
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
      size_t nwritten = scalar_decompose(code_point, out, &is_cc);
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
        size_t nwritten = scalar_decompose(code_point, out, &is_cc);
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
      size_t nwritten = scalar_decompose(code_point, out, &is_cc);
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

size_t scalar_normalize_utf8_nfd(const uint8_t *input, size_t length,
                                 uint8_t *out) {
  bool end_is_cc = false;
  return scalar_normalize_utf8_nfd_with_context(input, length, out, &end_is_cc);
}

static uint32_t scalar_multiply_shift_hash(uint32_t x) {
  uint32_t mul = x * 2654435761;
  uint32_t shift = mul >> 16;
  return shift & 65535;
}

static uint32_t scalar_xorshift_hash(uint32_t x, uint32_t seed) {
  x ^= seed;
  x ^= x >> 13;
  x ^= x << 17;
  x ^= x >> 5;
  return x;
}

static uint32_t scalar_xorshift_mul_hash(uint32_t x, uint32_t seed) {
  x ^= seed;
  x = ((x >> 16) ^ x) * 0x45D9F3B;
  x = ((x >> 16) ^ x) * 0x45D9F3B;
  x = (x >> 16) ^ x;
  return x;
}

static bool scalar_is_nfc_relevant(uint32_t code_point) {
  // It is relevant automatically if it is a Hangul code point
  if ((code_point >= NORMDATA_S_BASE &&
       code_point < NORMDATA_S_BASE + NORMDATA_S_COUNT) ||
      (code_point >= NORMDATA_L_BASE &&
       code_point < NORMDATA_L_BASE + NORMDATA_L_COUNT)) {
    return true;
  }

  uint32_t h1 = scalar_multiply_shift_hash(code_point);
  uint32_t h2 = scalar_xorshift_hash(code_point, 0xDEADBEEF);
  uint32_t h3 = scalar_xorshift_mul_hash(code_point, 0x4B71D390);
  uint32_t block_idx = h1 % 2048;
  uint32_t shift1 = h2 % 32;
  uint32_t shift2 = h3 % 32;
  uint32_t mask = 0;
  mask |= 1 << shift1;
  mask |= 1 << shift2;

  uint32_t block = NORMDATA_NFC_QC_BLOOM_FILTER[block_idx];
  return (block & mask) == mask;
}

__attribute__((unused)) static void
scalar_print_code_points(const uint8_t *input, size_t length) {
  size_t p = 0;
  while (p < length) {
    uint8_t size;
    uint32_t c = scalar_parse_code_point(input + p, &size);
    printf("%u(p=%zu) ", c, p);
    p += size;
  }
  printf("\n");
}

// Shift the bytes in a byte buffer to the right by a certain amount.
static void scalar_shift_right(uint8_t *buf, size_t length, size_t amt) {
  for (uint8_t *i = buf + length - 1; i >= buf; i--) {
    *(i + amt) = *i;
  }
}

// Shift the bytes in a byte buffer to the left by a certain amount.
static void scalar_shift_left(uint8_t *buf, size_t length, size_t amt) {
  for (uint8_t *i = buf; i < buf + (length - amt); i++) {
    *i = *(i + amt);
  }
}

// Find a starter character in a UTF-8 buffer, searching from right to left.
static size_t scalar_rfind_starter(const uint8_t *input, size_t length) {
  size_t p = 0;
  while (p < length) {
    while (!scalar_is_leading_utf8_byte(input[length - p - 1])) {
      p++;
    }
    uint8_t size;
    uint32_t c = scalar_parse_code_point(input + (length - p - 1), &size);
    uint8_t ccc = scalar_lookup_ccc(c);
    // If we found a starter, then we're done
    if (ccc == 0) {
      return length - p - 1;
    }
    p++;
  }
  return -1;
}

size_t scalar_normalize_utf8_nfc(const uint8_t *input, size_t length,
                                 uint8_t *out) {
  uint8_t *start = out;
  size_t p = 0;
  uint8_t last_ccc = 0;

  while (p < length) {
    uint8_t size;
    uint32_t c = scalar_parse_code_point(input + p, &size);
    uint8_t ccc = scalar_lookup_ccc(c);

    // We can skip this character if it the combining classes are in the right
    // order and if it is irrelevant
    if (ccc <= last_ccc && !scalar_is_nfc_relevant(c)) {
      for (size_t i = 0; i < size; i++) {
        *out++ = input[p + i];
      }
      p += size;
      last_ccc = ccc;
      continue;
    }

    // TODO: probably not correct. What happens when we decompose this character
    //       and the ccc changes? We should set it to the final character in the
    //       decomposition.
    last_ccc = ccc;

    // This starter should be NFC irrelevant
    size_t previous_starter_pos = scalar_rfind_starter(input, p);
    if (previous_starter_pos == (size_t)-1) {
      previous_starter_pos = 0;
    }

    uint32_t next_irrelevant_starter_pos = p + size;
    // Find the next starter character that is NFC irrelevant
    while (next_irrelevant_starter_pos < length) {
      uint8_t next_irrelevant_starter_size;
      uint32_t next_irrelevant_starter = scalar_parse_code_point(
          input + next_irrelevant_starter_pos, &next_irrelevant_starter_size);
      uint8_t next_irrelevant_starter_ccc =
          scalar_lookup_ccc(next_irrelevant_starter);
      if (next_irrelevant_starter_ccc == 0 && !scalar_is_nfc_relevant(c)) {
        break;
      }
      next_irrelevant_starter_pos += next_irrelevant_starter_size;
    }

    // NOTE: scary!
    uint8_t *normalized_out = out - (p - previous_starter_pos);
    // NFD normalize a localized region in between the two starters that are NFC
    // irrelevant.
    size_t normalized_length = scalar_normalize_utf8_nfd(
        input + previous_starter_pos,
        next_irrelevant_starter_pos - previous_starter_pos, normalized_out);

    size_t normalized_pos = 0;
    uint8_t normalized_last_ccc = 255;
    // Iterate through each code point, seeking back until a starter is found
    // and trying to combine with that. This part of the algorithm closely
    // matches up with the spec. See:
    // https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G49614
    while (normalized_pos < normalized_length) {
      uint8_t normalized_size;
      uint32_t normalized_c = scalar_parse_code_point(
          normalized_out + normalized_pos, &normalized_size);
      uint8_t normalized_ccc = scalar_lookup_ccc(normalized_c);

      // Find the preceding starter. It should be NFC irrelevant
      // TODO: we can cache this
      size_t starter_pos = scalar_rfind_starter(normalized_out, normalized_pos);
      assert(starter_pos != normalized_pos);
      uint8_t starter_size;
      uint32_t starter =
          scalar_parse_code_point(normalized_out + starter_pos, &starter_size);

      // Skip if there's no starter before this character
      if (starter_pos == (size_t)-1) {
        normalized_pos += normalized_size;
        normalized_last_ccc = normalized_ccc;
        continue;
      }
      // Can't combine when we're blocked
      if (normalized_ccc <= normalized_last_ccc &&
          starter_pos + starter_size != normalized_pos) {
        normalized_pos += normalized_size;
        normalized_last_ccc = normalized_ccc;
        continue;
      }

      uint32_t composed;
      if (starter <= 0xFFFF && normalized_c <= 0xFFFF) {
        composed = scalar_try_compose_bmp(starter, normalized_c);
      } else {
        composed = normdata_compose_supplementary(starter, normalized_c);
      }
      // Skip if no composed character
      if (composed == 0) {
        normalized_pos += normalized_size;
        normalized_last_ccc = normalized_ccc;
        continue;
      }
      uint8_t composed_size = scalar_code_point_size(composed);
      assert(composed_size >= starter_size);

      // Shift left to delete the combinnig character
      scalar_shift_left(normalized_out + normalized_pos,
                        normalized_length - normalized_pos, normalized_size);
      // Account for combining character deletion
      normalized_length -= normalized_size;

      // Shift everything right to make room for new composed code point
      scalar_shift_right(normalized_out + starter_pos + starter_size,
                         normalized_length - starter_pos - starter_size,
                         composed_size - starter_size);
      // Overwrite the starter with the new composed code point
      (void)scalar_write_code_point(composed, normalized_out + starter_pos);
      normalized_length += composed_size - starter_size;
      normalized_pos += composed_size - starter_size;
    }

    // Set the out pointer to the end of the normalized buffer
    out = normalized_out + normalized_length;
    // Set the input offset to the next starter that is garuanteed to not be
    // relevant to NFC
    p = next_irrelevant_starter_pos;
  }

  return out - start;
}
