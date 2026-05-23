// amalgamate add: #if XXUTF_IMPLEMENTATION_NEON

#include "impl/neon.h"
#include "impl/neon/neon_common.h"
#include "impl/scalar.h"
#include "unidata.h"
#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static size_t neon_casefold_masked_utf8(const uint8_t *input, uint64_t mask,
                                        uint8_t **out) {
  uint8x16_t in = vld1q_u8(input);

  // Check for 2-16 ASCII bytes
  int t1 = __builtin_ctzll(~mask);
  if (t1 > 2) {
    size_t min = t1 > 16 ? 16 : t1;
    uint8x16_t shifted = vsubq_u8(in, vdupq_n_u8('A'));
    uint8x16_t uppercase_mask = vcleq_u8(shifted, vdupq_n_u8('Z' - 'A'));
    // Add 32 to make uppercase ASCII turn into lowercase ASCII
    uint8x16_t casefold =
        vbslq_u8(uppercase_mask, vaddq_u8(in, vdupq_n_u8(32)), in);
    vst1q_u8(*out, casefold);
    *out += min;
    return min;
  }

  uint16_t sml_mask = mask & 0xFFF;
  uint16x4_t chars;
  size_t nbytes;

  if (sml_mask == 0x924) {
    chars = neon_parse_3_byte_utf8(in);
    nbytes = 12;
  } else if ((sml_mask & 0xFF) == 0xAA) {
    chars = neon_parse_2_byte_utf8(in);
    nbytes = 8;
  } else {
    uint8_t idx = UNIDATA_CODE_POINT_INDEX[sml_mask][0];
    nbytes = UNIDATA_CODE_POINT_INDEX[sml_mask][1];
    if (idx < UNIDATA_SHUFUTF8_INDEX_12) {
      chars = neon_parse_4_12_utf8(in, idx);
    } else if (idx < UNIDATA_SHUFUTF8_INDEX_123) {
      chars = neon_parse_4_123_utf8(in, idx);
    } else {
      *out += scalar_casefold_utf8(input, nbytes, *out);
      return nbytes;
    }
  }

  uint16x4_t values = NEON_TRIE_LOOKUP(UNIDATA_UTF8_CASEFOLD_TRIE, chars);
  if (vmaxv_u16(values) == 0) {
    vst1q_u8(*out, in);
    *out += nbytes;
    return nbytes;
  }

  for (size_t i = 0; i < 4; i++) {
    uint8_t leading = input[0];
    uint8_t size = UNIDATA_UTF8_SIZE[leading];
    uint16_t value = values[i];
    if (value == 0) {
      vst1_u8(*out, vld1_u8(input));
      *out += size;
      input += size;
      continue;
    }
    uint8_t length = value >> 12;
    const uint8_t *casefold_offset = &UNIDATA_UTF8_CASEFOLD_DATA[value & 0xFFF];
    vst1_u8(*out, vld1_u8(casefold_offset));
    *out += length;
    input += size;
  }

  return nbytes;
}

size_t neon_casefold_utf8(const uint8_t *input, size_t length, uint8_t *out) {
  uint8_t **out_ptr = &out;
  uint8_t *start = out;

  const size_t SAFETY_MARGIN = 16;
  size_t p = 0;
  while (p + 64 + SAFETY_MARGIN <= length) {
    uint64_t mask = neon_make_utf8_code_point_mask(input + p);
    size_t pmax = (p + 64) - 12;
    while (p < pmax) {
      size_t consumed = neon_casefold_masked_utf8(input + p, mask, out_ptr);
      p += consumed;
      mask >>= consumed;
    }
  }

  if (p < length) {
    *out_ptr += scalar_casefold_utf8(input + p, length - p, *out_ptr);
  }

  return *out_ptr - start;
}

static size_t neon_casefold_masked_utf8_length(const uint8_t *input,
                                               uint64_t mask,
                                               size_t *out_length) {
  uint8x16_t in = vld1q_u8(input);
  // Check for ASCII bytes
  int t1 = __builtin_ctzll(~mask);
  if (t1 > 0) {
    size_t min = t1 > 52 ? 52 : t1;
    *out_length += min;
    return min;
  }
  uint16_t sml_mask = mask & 0xFFF;
  uint16x4_t chars;
  size_t n_bytes;
  if (sml_mask == 0x924) {
    chars = neon_parse_3_byte_utf8(in);
    n_bytes = 12;
  } else if ((sml_mask & 0xFF) == 0xAA) {
    chars = neon_parse_2_byte_utf8(in);
    n_bytes = 8;
  } else {
    uint8_t idx = UNIDATA_CODE_POINT_INDEX[sml_mask][0];
    n_bytes = UNIDATA_CODE_POINT_INDEX[sml_mask][1];
    if (idx < UNIDATA_SHUFUTF8_INDEX_12) {
      chars = neon_parse_4_12_utf8(in, idx);
    } else if (idx < UNIDATA_SHUFUTF8_INDEX_123) {
      chars = neon_parse_4_123_utf8(in, idx);
    } else {
      *out_length += scalar_casefold_utf8_length(input, n_bytes);
      return n_bytes;
    }
  }
  uint16x4_t values =
      NEON_TRIE_LOOKUP(UNIDATA_UTF8_CASEFOLD_LENGTH_TRIE, chars);
  *out_length += vaddv_u16(values);
  return n_bytes;
}

size_t neon_casefold_utf8_length(const uint8_t *input, size_t length) {
  size_t out_length = 0;
  const size_t SAFETY_MARGIN = 16;
  size_t p = 0;
  while (p + 64 + SAFETY_MARGIN <= length) {
    uint64_t mask = neon_make_utf8_code_point_mask(input + p);
    size_t pmax = (p + 64) - 12;
    while (p < pmax) {
      size_t consumed =
          neon_casefold_masked_utf8_length(input + p, mask, &out_length);
      p += consumed;
      mask >>= consumed;
    }
  }
  if (p < length) {
    out_length += scalar_casefold_utf8_length(input + p, length - p);
  }
  return out_length;
}

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
