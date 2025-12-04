#include "impl/scalar/scalar_common.h"
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

// Check if a code point is in the Hangul block.
bool scalar_is_hangul(uint32_t code_point) {
  return code_point >= NORMDATA_S_BASE &&
         code_point < NORMDATA_S_BASE + NORMDATA_S_COUNT;
}

// Reverse a subsection of an array.
void scalar_reverse(uint8_t *array, size_t start, size_t end) {
  while (start < end) {
    uint8_t tmp = array[start];
    array[start] = array[end];
    array[end] = tmp;
    start++;
    end--;
  }
}

// Rotate a subsection of an array to the right by k positions.
void scalar_rotate(uint8_t *array, size_t size, size_t k) {
  scalar_reverse(array, 0, size - 1);
  scalar_reverse(array, 0, k - 1);
  scalar_reverse(array, k, size - 1);
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

// Try to compose two BMP code points into a single code point. Returns the
// composed code point if the composition is valid, or zero if the composition
// is not valid.
uint32_t scalar_try_compose_bmp(uint16_t c1, uint16_t c2) {
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

bool scalar_is_nfc_relevant(uint32_t code_point) {
  uint32_t h1 = scalar_multiply_shift_hash(code_point);
  uint32_t h2 = scalar_xorshift_hash(code_point);
  uint32_t h3 = scalar_xorshift_mul_hash(code_point ^ 0xDEADBEEFUL);
  uint32_t h4 = scalar_xorshift_mul_hash(code_point);
  uint32_t block_idx = h1 % 4096;
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

bool scalar_is_nfkc_relevant(uint32_t code_point) {
  uint32_t h1 = scalar_multiply_shift_hash(code_point);
  uint32_t h2 = scalar_xorshift_hash(code_point);
  uint32_t h3 = scalar_xorshift_mul_hash(code_point ^ 0xDEADBEEFUL);
  uint32_t h4 = scalar_xorshift_mul_hash(code_point);
  uint32_t block_idx = h1 % 4096;
  uint32_t shift1 = h2 % 32;
  uint32_t shift2 = h3 % 32;
  uint32_t shift3 = h4 % 32;
  uint32_t mask = 0;
  mask |= 1u << shift1;
  mask |= 1u << shift2;
  mask |= 1u << shift3;

  uint32_t block = NORMDATA_NFKC_BLOOM_FILTER[block_idx];
  return (block & mask) == mask;
}

// Shift the bytes in a byte buffer to the right by a certain amount.
void scalar_shift_right(uint8_t *buf, size_t length, size_t amt) {
  for (uint8_t *i = buf + length - 1; i >= buf; i--) {
    *(i + amt) = *i;
  }
}

// Shift the bytes in a byte buffer to the left by a certain amount.
void scalar_shift_left(uint8_t *buf, size_t length, size_t amt) {
  for (uint8_t *i = buf; i < buf + (length - amt); i++) {
    *i = *(i + amt);
  }
}
