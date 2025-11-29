#include "impl/scalar.h"
#include "impl/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

size_t scalar_copy_code_points(const uint8_t *input, uint8_t *out, size_t amt) {
  uint8_t *start = out;
  for (size_t i = 0; i < amt; i++) {
    uint8_t size = NORMDATA_UTF8_SIZE[input[0]];
    for (uint8_t j = 0; j < size; j++) {
      *out++ = input[j];
    }
    input += size;
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
  // Check if we have an LV syllable and a T jamo. Note that we check c2 >
  // NORMDATA_T_BASE, not c2 >= NORMDATA_T_BASE for a good reason: the first
  // valid T jamo is NORMDATA_T_BASE + 1! The spec defines the T base constant
  // to be off by one in order to make the math for algorithmic decomposition
  // cleaner.
  //
  // See:
  // https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/#G59434
  if (c1 >= NORMDATA_S_BASE && c1 < NORMDATA_S_BASE + NORMDATA_S_COUNT &&
      (c1 - NORMDATA_S_BASE) % NORMDATA_T_COUNT == 0 && c2 > NORMDATA_T_BASE &&
      c2 < NORMDATA_T_BASE + NORMDATA_T_COUNT) {
    return c1 + (c2 - NORMDATA_T_BASE);
  }

  uint32_t wide = c1;
  uint32_t key = (wide << 16) | c2;
  uint32_t salt_hash = scalar_phash(key, 0, NORMDATA_NFC_TABLE_SIZE);
  uint32_t salt = NORMDATA_NFC_SALT[salt_hash];
  uint32_t key_hash = scalar_phash(key, salt, NORMDATA_NFC_TABLE_SIZE);
  uint32_t k = NORMDATA_NFC_KV[key_hash][1];
  uint32_t comp = NORMDATA_NFC_KV[key_hash][0];
  if (k == key) {
    // The composition is valid, return the composed code point
    return comp;
  } else {
    return 0;
  }
}

#define DECOMP_SUFFIX nfd
#define DECOMP_TABLE_NAME NFD
#include "impl/scalar_impl.c" // amalgamate no_include
#undef DECOMP_SUFFIX
#undef DECOMP_TABLE_NAME

#define DECOMP_SUFFIX nfkd
#define DECOMP_TABLE_NAME NFKD
#include "impl/scalar_impl.c" // amalgamate no_include
#undef DECOMP_SUFFIX
#undef DECOMP_TABLE_NAME

static uint32_t scalar_multiply_shift_hash(uint32_t x) {
  uint32_t mul = x * 2654435761UL;
  uint32_t shift = mul >> 16;
  return shift & 65535;
}

static uint32_t scalar_xorshift_hash(uint32_t x) {
  x ^= x >> 13;
  x ^= x << 17;
  x ^= x >> 5;
  return x;
}

static uint32_t scalar_xorshift_mul_hash(uint32_t x) {
  x = ((x >> 16) ^ x) * 0x45D9F3BUL;
  x = ((x >> 16) ^ x) * 0x45D9F3BUL;
  x = (x >> 16) ^ x;
  return x;
}

static bool scalar_is_nfc_relevant(uint32_t code_point) {
  uint32_t h1 = scalar_multiply_shift_hash(code_point ^ 0xDEADBEEFUL);
  uint32_t h2 = scalar_xorshift_hash(code_point);
  uint32_t h3 = scalar_xorshift_mul_hash(code_point);
  uint32_t h4 = h2 + 3 * h3;
  uint32_t block_idx = h1 % 2048;
  uint32_t shift1 = h2 % 32;
  uint32_t shift2 = h3 % 32;
  uint32_t shift3 = h4 % 32;
  uint32_t mask = 0;
  mask |= 1u << shift1;
  mask |= 1u << shift2;
  mask |= 1u << shift3;

  uint32_t block = NORMDATA_NFC_BLOOM_FILTER[block_idx];
  return (block & mask) == mask;
}

__attribute__((unused)) void scalar_print_code_points(const uint8_t *input,
                                                      size_t length) {
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
size_t scalar_rfind_starter(const uint8_t *input, size_t length) {
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

// Find the next starter character that is NFC irrelevant, or -1 if one is not
// found.
size_t scalar_find_nfc_irrelevant_starter(const uint8_t *input, size_t length) {
  uint32_t p = 0;
  while (p < length) {
    uint8_t size;
    uint32_t c = scalar_parse_code_point(input + p, &size);
    uint8_t ccc = scalar_lookup_ccc(c);
    if (ccc == 0 && !scalar_is_nfc_relevant(c)) {
      return p;
    }
    p += size;
  }

  return (size_t)-1;
}

size_t scalar_normalize_utf8_nfc(const uint8_t *input, size_t length,
                                 uint8_t *out) {
  uint8_t *start = out;
  size_t p = 0;
  uint8_t last_ccc = 0;

  while (p < length) {
    uint8_t size;
    uint32_t c = scalar_parse_code_point(input + p, &size);

    // ASCII fast path to skip ccc lookup
    if (c <= 0x7F) {
      *out++ = (uint8_t)c;
      p++;
      last_ccc = 0;
      continue;
    }

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

    size_t next_irrelevant_starter_pos =
        scalar_find_nfc_irrelevant_starter(input + p + size, length - p - size);
    if (next_irrelevant_starter_pos == (size_t)-1) {
      next_irrelevant_starter_pos = length;
    } else {
      next_irrelevant_starter_pos += p + size;
    }

    // NOTE: scary!
    uint8_t *normalized_out = out - (p - previous_starter_pos);
    // NFD normalize a localized region in between the two starters that are NFC
    // irrelevant. This guarantees that, if we NFC normalize this range, no
    // characters after the end of the range in the input will combine/interact
    // with the range we normalized. In other words, we run NFC on the largest
    // possible sub-range of characters that may (or may not) have to do with
    // the NFC relevant character `c` that we initially detected.
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
      // Skip if we don't have a starter before this
      if (starter_pos == (size_t)-1) {
        normalized_pos += normalized_size;
        normalized_last_ccc = normalized_ccc;
        continue;
      }

      uint8_t starter_size;
      uint32_t starter =
          scalar_parse_code_point(normalized_out + starter_pos, &starter_size);
      // Skip if we're blocked from the starter
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
