#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
  if (code_point <= 0xFFFF) {
    uint16_t shift = code_point >> 6;
    uint16_t masked = code_point & 63;
    uint16_t index = NORMDATA_CCC_TRIE_INDEX[shift];
    uint8_t value = NORMDATA_CCC_TRIE_DATA[index + masked];
    return value;
  }
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

#define SCALAR_NORMALIZATION_HELPERS(decomp_form, decomp_form_upper,           \
                                     comp_form, comp_form_upper)               \
  bool scalar_is_##comp_form##_relevant(uint32_t code_point) {                 \
    if (code_point <= 0xFFFF) {                                                \
      uint16_t shift = code_point >> 6;                                        \
      uint16_t masked = code_point & 63;                                       \
      uint16_t index = NORMDATA_##comp_form_upper##_TRIE_INDEX[shift];         \
      uint8_t value = NORMDATA_##comp_form_upper##_TRIE_DATA[index + masked];  \
      return value > 0;                                                        \
    }                                                                          \
    uint32_t h1 = scalar_multiply_shift_hash(code_point);                      \
    uint32_t h2 = scalar_xorshift_hash(code_point);                            \
    uint32_t h3 = scalar_xorshift_mul_hash(code_point ^ 0xDEADBEEFUL);         \
    uint32_t h4 = scalar_xorshift_mul_hash(code_point);                        \
    const uint32_t COMP_BLOOM_SIZE =                                           \
        sizeof(NORMDATA_##comp_form_upper##_BLOOM_FILTER) / sizeof(uint32_t);  \
    uint32_t block_idx = h1 % COMP_BLOOM_SIZE;                                 \
    uint32_t shift1 = h2 % 32;                                                 \
    uint32_t shift2 = h3 % 32;                                                 \
    uint32_t shift3 = h4 % 32;                                                 \
    uint32_t mask = 0;                                                         \
    mask |= 1u << shift1;                                                      \
    mask |= 1u << shift2;                                                      \
    mask |= 1u << shift3;                                                      \
                                                                               \
    uint32_t block = NORMDATA_##comp_form_upper##_BLOOM_FILTER[block_idx];     \
    return (block & mask) == mask;                                             \
  }                                                                            \
                                                                               \
  bool scalar_is_##decomp_form##_relevant(uint32_t code_point) {               \
    if (code_point <= 0xFFFF) {                                                \
      uint16_t shift = code_point >> 6;                                        \
      uint16_t masked = code_point & 63;                                       \
      uint16_t index = NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[shift];  \
      uint8_t value =                                                          \
          NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[index + masked];       \
      return value > 0;                                                        \
    }                                                                          \
    uint32_t salt_hash = scalar_phash(                                         \
        code_point, 0, NORMDATA_##decomp_form_upper##_TABLE_SIZE);             \
    uint32_t salt = NORMDATA_##decomp_form_upper##_SALT[salt_hash];            \
    uint32_t key_hash = scalar_phash(                                          \
        code_point, salt, NORMDATA_##decomp_form_upper##_TABLE_SIZE);          \
    NormdataTableEntry kv = NORMDATA_##decomp_form_upper##_KV[key_hash];       \
    return kv.k == code_point;                                                 \
  }

SCALAR_NORMALIZATION_HELPERS(nfd, NFD, nfc, NFC);
SCALAR_NORMALIZATION_HELPERS(nfkd, NFKD, nfkc, NFKC);

#undef SCALAR_NORMALIZATION_HELPERS

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

size_t scalar_write_code_point_utf8(uint32_t code_point, uint8_t *utf8_bytes) {
  if (code_point <= 0x7F) {
    utf8_bytes[0] = (uint8_t)(code_point & 0xFF);
    return 1;
  } else if (code_point <= 0x7FF) {
    utf8_bytes[0] = 0xC0 | (code_point >> 6);
    utf8_bytes[1] = 0x80 | (code_point & 0x3F);
    return 2;
  } else if (code_point <= 0xFFFF) {
    utf8_bytes[0] = 0xE0 | (code_point >> 12);
    utf8_bytes[1] = 0x80 | ((code_point >> 6) & 0x3F);
    utf8_bytes[2] = 0x80 | (code_point & 0x3F);
    return 3;
  } else if (code_point <= 0x10FFFF) {
    utf8_bytes[0] = 0xF0 | (code_point >> 18);
    utf8_bytes[1] = 0x80 | ((code_point >> 12) & 0x3F);
    utf8_bytes[2] = 0x80 | ((code_point >> 6) & 0x3F);
    utf8_bytes[3] = 0x80 | (code_point & 0x3F);
    return 4;
  } else {
    // Code point is too large, but we don't handle errors
    return 0;
  }
}

size_t scalar_code_point_size_utf8(uint32_t code_point) {
  if (code_point <= 0x7F) {
    return 1;
  } else if (code_point <= 0x7FF) {
    return 2;
  } else if (code_point <= 0xFFFF) {
    return 3;
  } else if (code_point <= 0x10FFFF) {
    return 4;
  } else {
    // Code point is too large, but we don't handle errors
    return 0;
  }
}

uint32_t scalar_parse_code_point_utf8(const uint8_t *input, uint8_t *size) {
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

bool scalar_is_leading_utf8_byte(uint8_t b) {
  return (b & 0b11000000) != 0b10000000;
}

void scalar_print_code_points_utf8(const uint8_t *input, size_t length) {
  size_t p = 0;
  while (p < length) {
    uint8_t size;
    uint32_t c = scalar_parse_code_point_utf8(input + p, &size);
    printf("%u(p=%zu) ", c, p);
    p += size;
  }
  printf("\n");
}

size_t scalar_count_code_points_utf8(const uint8_t *buf, size_t length) {
  size_t count = 0;
  size_t p = 0;
  while (p < length) {
    p += NORMDATA_UTF8_SIZE[buf[p]];
    count++;
  }
  return count;
}

size_t scalar_get_code_point_pos_reverse_utf8(const uint8_t *buf, size_t length,
                                              size_t n) {
  if (n == 0) {
    return 0;
  }
  size_t count = n;
  size_t p = 0;
  while (p < length) {
    while (!scalar_is_leading_utf8_byte(*((buf - p) - 1))) {
      p++;
    }
    count--;
    if (count == 0) {
      return p + 1;
    }
    p++;
  }
  return -1;
}

void scalar_write_uint16le(uint16_t x, uint8_t *out) {
  out[0] = (uint8_t)(x & 0xFF);
  out[1] = (uint8_t)(x >> 8);
}

void scalar_write_uint16be(uint16_t x, uint8_t *out) {
  out[0] = (uint8_t)(x >> 8);
  out[1] = (uint8_t)(x & 0xFF);
}

uint16_t scalar_read_uint16le(const uint8_t *input) {
  return (uint16_t)input[0] | (uint16_t)input[1] << 8;
}

uint16_t scalar_read_uint16be(const uint8_t *input) {
  return ((uint16_t)input[0] << 8) | (uint16_t)input[1];
}

size_t scalar_code_point_size_utf16(uint32_t code_point) {
  return code_point <= 0xFFFF ? 2 : 4;
}

bool scalar_is_utf16_low_surrogate(uint16_t code_unit) {
  return code_unit >= 0xDC00 && code_unit <= 0xDFFF;
}

bool scalar_is_utf16_high_surrogate(uint16_t code_unit) {
  return code_unit >= 0xD800 && code_unit <= 0xDBFF;
}

#define SCALAR_UTF16_HELPERS(endianness)                                       \
  size_t scalar_write_code_point_utf16##endianness(uint32_t code_point,        \
                                                   uint8_t *utf16_bytes) {     \
    /* Check if in BMP */                                                      \
    if (code_point <= 0xFFFF) {                                                \
      uint16_t u = (uint16_t)code_point;                                       \
      scalar_write_uint16##endianness(u, utf16_bytes);                         \
      return 2;                                                                \
    }                                                                          \
    code_point -= 0x10000;                                                     \
    uint16_t high = 0xD800 | (code_point >> 10);                               \
    uint16_t low = 0xDC00 | (code_point & 0x3FF);                              \
    scalar_write_uint16##endianness(high, utf16_bytes);                        \
    scalar_write_uint16##endianness(low, utf16_bytes + 2);                     \
    return 4;                                                                  \
  }                                                                            \
                                                                               \
  uint32_t scalar_parse_code_point_utf16##endianness(const uint8_t *input,     \
                                                     uint8_t *size) {          \
    uint16_t w1 = scalar_read_uint16##endianness(input);                       \
    if (scalar_is_utf16_high_surrogate(w1)) {                                  \
      uint16_t w2 = scalar_read_uint16##endianness(input + 2);                 \
      uint32_t cp =                                                            \
          (((uint32_t)(w1 - 0xD800) << 10) | ((uint32_t)(w2 - 0xDC00))) +      \
          0x10000;                                                             \
      *size = 4;                                                               \
      return cp;                                                               \
    }                                                                          \
    *size = 2;                                                                 \
    return w1;                                                                 \
  }                                                                            \
                                                                               \
  void scalar_print_code_points_utf16##endianness(const uint8_t *input,        \
                                                  size_t length) {             \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint8_t size;                                                            \
      uint32_t c =                                                             \
          scalar_parse_code_point_utf16##endianness(input + p, &size);         \
      printf("%u(p=%zu) ", c, p);                                              \
      p += size;                                                               \
    }                                                                          \
    printf("\n");                                                              \
  }                                                                            \
                                                                               \
  size_t scalar_count_code_points_utf16##endianness(const uint8_t *buf,        \
                                                    size_t length) {           \
    size_t count = 0;                                                          \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint16_t code_unit = scalar_read_uint16##endianness(buf + p);            \
      p += scalar_is_utf16_high_surrogate(code_unit) ? 4 : 2;                  \
      count++;                                                                 \
    }                                                                          \
    return count;                                                              \
  }                                                                            \
                                                                               \
  size_t scalar_get_code_point_pos_reverse_utf16##endianness(                  \
      const uint8_t *buf, size_t length, size_t n) {                           \
    if (n == 0) {                                                              \
      return 0;                                                                \
    }                                                                          \
    size_t count = n;                                                          \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint16_t code_unit = scalar_read_uint16##endianness(buf - p - 2);        \
      if (scalar_is_utf16_low_surrogate(code_unit)) {                          \
        p += 2;                                                                \
        continue;                                                              \
      }                                                                        \
      count--;                                                                 \
      if (count == 0) {                                                        \
        return p + 2;                                                          \
      }                                                                        \
      p += 2;                                                                  \
    }                                                                          \
    return -1;                                                                 \
  }

SCALAR_UTF16_HELPERS(le);
SCALAR_UTF16_HELPERS(be);

#undef SCALAR_UTF16_HELPERS
