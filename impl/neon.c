// amalgamate add: #if UTF8NORM_IMPLEMENTATION_NEON

#include "impl/neon.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_acle.h>
#include <arm_neon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Create an 8-bit movemask from a 16x4 vector.
static inline uint8_t neon_movemask_u16(uint16x4_t v) {
  const uint16x4_t mask = {0x1, 0x2, 0x4, 0x8};
  uint16x4_t mv = vand_u16(v, mask);
  return (uint8_t)(vaddv_u16(mv) & 0xF);
}

// Macro to define a print function for an arbitrarily-shaped NEON vector.
#define NEON_PRINT_FUNC(type, child_type, store_func)                          \
  __attribute__((unused)) static void neon_print_##type(const char *name,      \
                                                        type vec) {            \
    child_type values[sizeof(type) / sizeof(child_type)];                      \
    store_func(values, vec);                                                   \
    printf("%s: ", name);                                                      \
    for (uint8_t i = 0; i < sizeof(values) / sizeof(child_type); i++) {        \
      printf("%04x ", values[i]);                                              \
    }                                                                          \
    printf("\n");                                                              \
  }

NEON_PRINT_FUNC(uint8x16_t, uint8_t, vst1q_u8);
NEON_PRINT_FUNC(uint8x8_t, uint8_t, vst1_u8);
NEON_PRINT_FUNC(uint16x8_t, uint16_t, vst1q_u16);
NEON_PRINT_FUNC(uint16x4_t, uint16_t, vst1_u16);
NEON_PRINT_FUNC(uint32x4_t, uint32_t, vst1q_u32);
NEON_PRINT_FUNC(uint32x2_t, uint32_t, vst1_u32);

// Parse four three-byte UTF-8 code points into their 16-bit code point values.
// Taken from simdutf
static uint16x4_t neon_parse_three_byte_utf8(uint8x16_t in) {
  const uint8x16_t sh = {0, 2, 3, 5, 6, 8, 9, 11, 1, 1, 4, 4, 7, 7, 10, 10};
  uint8x16_t perm = vqtbl1q_u8(in, sh);
  // Split into half vectors.
  // 10cccccc|1110aaaa
  uint8x8_t perm_low = vget_low_u8(perm); // no-op
  // 10bbbbbb|10bbbbbb
  uint8x8_t perm_high = vget_high_u8(perm);
  // xxxxxxxx 10bbbbbb
  uint16x4_t mid = vreinterpret_u16_u8(perm_high); // no-op
  // xxxxxxxx 1110aaaa
  uint16x4_t high = vreinterpret_u16_u8(perm_low); // no-op
  // Assemble with shift left insert.
  // xxxxxxaa aabbbbbb
  uint16x4_t mid_high = vsli_n_u16(mid, high, 6);
  // (perm_low << 8) | (perm_low >> 8)
  // xxxxxxxx 10cccccc
  uint16x4_t low = vreinterpret_u16_u8(vrev16_u8(perm_low));
  // Shift left insert into the low bits
  // aaaabbbb bbcccccc
  uint16x4_t composed = vsli_n_u16(low, mid_high, 6);
  return composed;
}

// Parse four two-byte UTF-8 code points into their 16-bit code point values.
// Taken from simdutf
static uint16x4_t neon_parse_2_byte_utf8(uint8x16_t in) {
  // 10bbbbbb 110aaaaa
  uint16x8_t upper = vreinterpretq_u16_u8(in);
  // (in << 8) | (in >> 8)
  // 110aaaaa 10bbbbbb
  uint16x8_t lower = vreinterpretq_u16_u8(vrev16q_u8(in));
  // 00000000 000aaaaa
  uint16x8_t upper_masked = vandq_u16(upper, vmovq_n_u16(0x1F));
  // Assemble with shift left insert.
  // 00000aaa aabbbbbb
  uint16x8_t composed = vsliq_n_u16(lower, upper_masked, 6);
  return vget_low_u16(composed);
}

// Parse four code points encoded in UTF-8 into 16-bit code point values.
// Taken from simdutf
static uint16x4_t neon_parse_4_12_utf8(uint8x16_t in, size_t shufutf8_idx) {
  uint8x16_t sh = vld1q_u8(NORMDATA_SHUFUTF8[shufutf8_idx]);
  // Shuffle
  // 1 byte: 00000000 0bbbbbbb
  // 2 byte: 110aaaaa 10bbbbbb
  uint16x8_t perm = vreinterpretq_u16_u8(vqtbl1q_u8(in, sh));
  // Mask
  // 1 byte: 00000000 0bbbbbbb
  // 2 byte: 00000000 00bbbbbb
  uint16x8_t ascii = vandq_u16(perm, vmovq_n_u16(0x7f)); // 6 or 7 bits
  // 1 byte: 00000000 00000000
  // 2 byte: 000aaaaa 00000000
  uint16x8_t highbyte = vandq_u16(perm, vmovq_n_u16(0x1f00)); // 5 bits
  // Combine with a shift right accumulate
  // 1 byte: 00000000 0bbbbbbb
  // 2 byte: 00000aaa aabbbbbb
  uint16x8_t composed = vsraq_n_u16(ascii, highbyte, 2);
  return vget_low_u16(composed);
}

// Parse four code points encoded in UTF-8 into 16-bit code point values.
// Taken from simdutf
static uint16x4_t neon_parse_4_123_utf8(uint8x16_t in, size_t shufutf8_idx) {
  // UTF-16 and UTF-32 use similar algorithms, but UTF-32 skips the narrowing.
  uint8x16_t sh = vld1q_u8(NORMDATA_SHUFUTF8[shufutf8_idx]);
  // XXX: depending on the system scalar instructions might be faster.
  // 1 byte: 00000000 00000000 0ccccccc
  // 2 byte: 00000000 110bbbbb 10cccccc
  // 3 byte: 1110aaaa 10bbbbbb 10cccccc
  uint32x4_t perm = vreinterpretq_u32_u8(vqtbl1q_u8(in, sh));
  // 1 byte: 00000000 0ccccccc
  // 2 byte: xx0bbbbb x0cccccc
  // 3 byte: xxbbbbbb x0cccccc
  uint16x4_t lowperm = vmovn_u32(perm);
  // Partially mask with bic (doesn't require a temporary register unlike and)
  // The shift left insert below will clear the top bits.
  // 1 byte: 00000000 00000000
  // 2 byte: xx0bbbbb 00000000
  // 3 byte: xxbbbbbb 00000000
  uint16x4_t middlebyte = vbic_u16(lowperm, vmov_n_u16(0x00FF));
  // ASCII
  // 1 byte: 00000000 0ccccccc
  // 2+byte: 00000000 00cccccc
  uint16x4_t ascii = vand_u16(lowperm, vmov_n_u16(0x7F));
  // Split into narrow vectors.
  // 2 byte: 00000000 00000000
  // 3 byte: 00000000 xxxxaaaa
  uint16x4_t highperm = vshrn_n_u32(perm, 16);
  // Shift right accumulate the middle byte
  // 1 byte: 00000000 0ccccccc
  // 2 byte: 00xx0bbb bbcccccc
  // 3 byte: 00xxbbbb bbcccccc
  uint16x4_t middlelow = vsra_n_u16(ascii, middlebyte, 2);
  // Shift left and insert the top 4 bits, overwriting the garbage
  // 1 byte: 00000000 0ccccccc
  // 2 byte: 00000bbb bbcccccc
  // 3 byte: aaaabbbb bbcccccc
  uint16x4_t composed = vsli_n_u16(middlelow, highperm, 12);
  return composed;
}

// Write 8 code points, assuming they all expand to three bytes.
static void neon_write_8_3_byte_utf8(uint16x8_t in, uint8_t *out) {
  uint8x8x3_t bytes;

  // 1110xxxxxxxxxxxx
  uint16x8_t high = vsriq_n_u16(vdupq_n_u16(0xE000), in, 4);
  // 1110xxxx
  uint8x8_t high_narrow = vshrn_n_u16(high, 8);
  bytes.val[0] = high_narrow;

  // xxxxxxxx
  uint8x8_t middle = vshrn_n_u16(in, 6);
  // 00xxxxxx
  uint8x8_t middle_cleared = vand_u8(middle, vdup_n_u8(0b00111111));
  // 10xxxxxx
  bytes.val[1] = vorr_u8(middle_cleared, vdup_n_u8(0b10000000));

  // 0000000000xxxxxx
  uint16x8_t low = vandq_u16(in, vdupq_n_u16(0b00111111));
  uint8x8_t low_narrow = vmovn_u16(low);
  // 10xxxxxx
  bytes.val[2] = vorr_u8(low_narrow, vdup_n_u8(0b10000000));

  // Interleaved store into output
  vst3_u8(out, bytes);
}

// Extremely fast, low quality hash function
static uint32x4_t neon_mul_shift_hash(uint32x4_t x) {
  uint32x4_t mul = vmulq_n_u32(x, 2654435761);
  uint32x4_t shift = vshrq_n_u32(mul, 16);
  uint32x4_t y = vandq_u32(shift, vdupq_n_u32(65535));
  return y;
}

// Moderate quality hash function
static uint32x4_t neon_xorshift_hash(uint32x4_t x) {
  x = veorq_u32(x, vshrq_n_u32(x, 13));
  x = veorq_u32(x, vshlq_n_u32(x, 17));
  x = veorq_u32(x, vshrq_n_u32(x, 5));
  return x;
}

// High quality hash function based on the MurmurHash3 finalizer
static uint32x4_t neon_xorshift_mul_hash(uint32x4_t x) {
  x = vmulq_n_u32(veorq_u32(vshrq_n_u32(x, 16), x), 0x45D9F3B);
  x = vmulq_n_u32(veorq_u32(vshrq_n_u32(x, 16), x), 0x45D9F3B);
  x = veorq_u32(vshrq_n_u32(x, 16), x);
  return x;
}

// Pass a code point vector through the decomp+cc bloom filter, which will
// return a vector of 0s and 0xFFFFFFFFs probabilistically indicating whether
// the corresponding code point has a decomposition or is a combining character.
static uint32x4_t neon_evaluate_bloom(uint32x4_t input) {
  uint32x4_t h1 = neon_mul_shift_hash(input);
  uint32x4_t h2 = neon_xorshift_hash(input);
  uint32x4_t h3 = neon_xorshift_mul_hash(input);

  // h1 % 4096
  uint32x4_t block_idx = vandq_u32(h1, vdupq_n_u32(4095));
  // h2 % 32
  uint32x4_t shift1 = vandq_u32(h2, vdupq_n_u32(31));
  // h3 % 32
  uint32x4_t shift2 = vandq_u32(h3, vdupq_n_u32(31));
  // (h2 + h3) % 32
  uint32x4_t shift3 = vandq_u32(vaddq_u32(h2, h3), vdupq_n_u32(31));

  uint32x4_t mask = vshlq_u32(vdupq_n_u32(1), shift1);
  mask = vorrq_u32(mask, vshlq_u32(vdupq_n_u32(1), shift2));
  mask = vorrq_u32(mask, vshlq_u32(vdupq_n_u32(1), shift3));

  uint32x4_t block = {
      NORMDATA_BLOOM_FILTER[vgetq_lane_u32(block_idx, 0)],
      NORMDATA_BLOOM_FILTER[vgetq_lane_u32(block_idx, 1)],
      NORMDATA_BLOOM_FILTER[vgetq_lane_u32(block_idx, 2)],
      NORMDATA_BLOOM_FILTER[vgetq_lane_u32(block_idx, 3)],
  };

  uint32x4_t result = vandq_u32(mask, block);
  uint32x4_t result_eq = vceqq_u32(result, mask);

  return result_eq;
}

// Check if a code point vector contains Hangul syllables. The result is a
// vector of 0s and 0xFFFFFFFFs, where 0 means the code point is not a Hangul
// syllable and 0xFFFFFFFF means it is a Hangul syllable.
static inline uint32x4_t neon_hangul_mask(uint32x4_t input) {
  uint32x4_t ge = vcgeq_u32(input, vdupq_n_u32(NORMDATA_S_BASE));
  uint32x4_t lt =
      vcltq_u32(input, vdupq_n_u32(NORMDATA_S_BASE + NORMDATA_S_COUNT));
  uint32x4_t cmp = vandq_u32(lt, ge);
  return cmp;
}

// Size of UTF-8 code points in bytes, indexed by the first byte.
static const uint8_t neon_utf8_size[256] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 0};

// Decompose a 4x32-bit vector of code points into their UTF-8
// representations, writing them into the output buffer. The relevant mask
// indicates which code points should be decomposed (0 meaning irrelevant).
//
// This function assumes that the input code points are not Hangul syllables.
static void neon_decompose_non_hangul(uint32x4_t values, uint8x16_t in,
                                      uint32x4_t relevant, uint8_t **out,
                                      const uint8_t *input, bool *end_is_cc) {
  // Pre-emptively write the input into the output buffer. This is done so we
  // can skip copying the input bytes if the code point is not relevant until
  // we reach the first relevant code point. SIMD copying is generally much
  // faster than scalar copying.
  // This change leads to a solid 7% speedup on some benchmarks.
  vst1q_u8(*out, in);
  // The broken flag indicates whether we have encountered a code point that
  // requires decomposition. If we have not encountered such a code point, we
  // can skip the decomposition _and_ skip copying the input bytes since we've
  // done so above.
  bool broken = false;
  bool last_is_cc = *end_is_cc;
#pragma clang loop unroll(enable)
  for (size_t i = 0; i < 4; i++) {
    uint32_t v = values[i];
    bool r = relevant[i] > 0;

    uint8_t leading = input[0];
    uint8_t size = neon_utf8_size[leading];
    if (size == 1) {
      if (last_is_cc) {
        scalar_sort_characters(*out - 1);
      }
      // ASCII code point, no decomposition needed.
      *(*out)++ = leading;
      input++;
      last_is_cc = false;
      continue;
    }

    bool is_cc = false;

    size_t nwritten = 0;
    // If the code point is not relevant or, (if it is relevant) but
    // decomposition yields no results, we just copy the input
    if (broken && (!r || (nwritten = scalar_decompose(v, *out, &is_cc)) == 0)) {
      for (size_t j = 0; j < size; j++) {
        (*out)[j] = input[j];
      }
      nwritten = size;
    } else {
      broken = true;
    }
    if (last_is_cc && !is_cc) {
      scalar_sort_characters(*out - 1);
    }
    last_is_cc = is_cc;
    input += size;
    *out += nwritten;
  }

  *end_is_cc = last_is_cc;
}

// Compute the L, V, and T indices for Hangul syllable decomposition.
//
// https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401
static inline uint16x4x3_t neon_compute_hangul_jamo(uint16x4_t chars) {
  // Compute the S index
  uint16x4_t s = vsub_u16(chars, vdup_n_u16(NORMDATA_S_BASE));

  uint32x4_t l_fixed = vmull_n_u16(s, 28533);
  // Shift the fixed point number
  uint32x4_t l_wide = vshrq_n_u32(l_fixed, 24);
  // L index: s / N_COUNT
  uint16x4_t l = vmovn_u32(l_wide);

  // Multiply and subtract to get the remainder
  uint16x4_t v_modulo = vmls_n_u16(s, l, NORMDATA_N_COUNT);
  uint32x4_t v_fixed = vmull_n_u16(v_modulo, 2341);
  uint32x4_t v_wide = vshrq_n_u32(v_fixed, 16);
  // V index: (s % N_COUNT) / T_COUNT
  uint16x4_t v = vmovn_u32(v_wide);

  uint16x4_t t_shifted = vshr_n_u16(s, 2);
  uint32x4_t t_fixed = vmull_n_u16(t_shifted, 18725);
  // s / T_COUNT
  uint32x4_t t_div_wide = vshrq_n_u32(t_fixed, 17);
  uint16x4_t t_div = vmovn_u32(t_div_wide);
  // T index: s % T_COUNT
  uint16x4_t t = vmls_n_u16(s, t_div, NORMDATA_T_COUNT);

  uint16x4x3_t vals;
  vals.val[0] = vadd_u16(l, vdup_n_u16(NORMDATA_L_BASE));
  vals.val[1] = vadd_u16(v, vdup_n_u16(NORMDATA_V_BASE));
  vals.val[2] = vadd_u16(t, vdup_n_u16(NORMDATA_T_BASE));

  return vals;
}

// Write a 3-byte code point into the output buffer. The code point is assumed
// to be in the range of 0x0800 to 0xFFFF.
static inline void neon_write_3_byte_code_point(uint16_t codepoint,
                                                uint8_t *out) {
  out[0] = 0xE0 | (codepoint >> 12);
  out[1] = 0x80 | ((codepoint >> 6) & 0x3F);
  out[2] = 0x80 | (codepoint & 0x3F);
}

// Decompose a 4x32-bit vector of code points into their UTF-8 representations,
// writing them into the output buffer. The relevant mask indicates which code
// points should be decomposed as Hangul (0 meaning irrelevant). The irrelevant
// code points are copied as-is from the input buffer.
//
// This function assumes that the input code points are Hangul syllables.
static void neon_decompose_hangul(uint32x4_t values, uint32x4_t relevant,
                                  uint8_t **out, const uint8_t *input,
                                  bool *end_is_cc) {
  if (*end_is_cc) {
    scalar_sort_characters(*out - 1);
  }
  *end_is_cc = false;

  // Decompose everthing (assuming they're all Hangul syllables). This
  // assumption is made because, empirically, most of the time this function is
  // called, it is because there is a single ASCII space in the input vector,
  // which causes a branch miss for the pure Hangul code path, and puts is in
  // this path. Therefore, we can still eagerly compute the Hangul jamo values,
  // and then only write the relevant ones.
  uint16x4_t chars = vmovn_u32(values);
  uint16x4x3_t lvt = neon_compute_hangul_jamo(chars);

#pragma clang loop unroll(enable)
  for (size_t i = 0; i < 4; i++) {
    if (relevant[i] == 0) {
      // Not a Hangul syllable, just copy the input.
      size_t size = neon_utf8_size[input[0]];
      for (size_t j = 0; j < size; j++) {
        *(*out)++ = input[j];
      }
      input += size;
      continue;
    }

    uint16_t l = lvt.val[0][i];
    uint16_t v = lvt.val[1][i];
    uint16_t t = lvt.val[2][i];

    neon_write_3_byte_code_point(l, *out);
    *out += 3;
    neon_write_3_byte_code_point(v, *out);
    *out += 3;
    // Naively write the T code point, even if it is zero, and branchlessly
    // increment the output pointer if it is non-zero. Although this appears
    // like extra work, it is actually faster than branching on the T code point
    // being zero or not, because the branch miss penalty is quite high. For the
    // korean.txt benchmark, this gave a ~33% speedup.
    neon_write_3_byte_code_point(t, *out);
    *out += 3 * (t - NORMDATA_T_BASE > 0);
    input += 3;
  }
}

// Copy a 16-byte input vector into the output buffer.
static inline void neon_skip(uint8x16_t in, size_t nchars, uint8_t **out,
                             bool *end_is_cc) {
  if (*end_is_cc) {
    scalar_sort_characters(*out - 1);
  }
  *end_is_cc = false;
  vst1q_u8(*out, in);
  *out += nchars;
}

// Generalized decomposition for a 16-byte input vector of UTF-8 code points.
// The parsed code points are passed in as a 4x32-bit vector of code points,
// and the `nchars` parameter indicates how many bytes of the input vector are
// used for the `chars` parameter. The `input` parameter is the original pointer
// to UTF-8 bytes.
static inline void neon_decompose(uint8x16_t in, uint32x4_t chars,
                                  size_t nchars, const uint8_t *input,
                                  uint8_t **out, bool *end_is_cc) {
  uint32x4_t bloom = neon_evaluate_bloom(chars);
  uint32x4_t hangul_mask = neon_hangul_mask(chars);
  // We use these results to split the input into four cases that have
  // specialized handling for each case.
  bool bloom_result = vmaxvq_u32(bloom) > 0;
  bool hangul_result = vmaxvq_u32(hangul_mask) > 0;
  if (!bloom_result && !hangul_result) {
    // Case where we have no precomposed characters and no Hangul syllables
    neon_skip(in, nchars, out, end_is_cc);
  } else if (hangul_result && !bloom_result) {
    // Case where we have Hangul syllables, but no precomposed characters
    neon_decompose_hangul(chars, hangul_mask, out, input, end_is_cc);
  } else if (bloom_result && !hangul_result) {
    // Case where we have precomposed characters, but no Hangul
    neon_decompose_non_hangul(chars, in, bloom, out, input, end_is_cc);
  } else {
    // Case where we have both precomposed characters and Hangul syllables.
    // Very rare in practice, so we just fall back to the scalar implementation.
    *out +=
        scalar_normalize_utf8_nfd_with_context(input, nchars, *out, end_is_cc);
  }
}

// memcpy for inputs less than 64 bytes large.
static inline void neon_memcpy_small(uint8_t *dst, const uint8_t *src,
                                     size_t len) {
  vst1q_u8(dst, vld1q_u8(src));
  if (len <= 16) {
    return;
  }
  vst1q_u8(dst + 16, vld1q_u8(src + 16));
  if (len <= 32) {
    return;
  }
  vst1q_u8(dst + 32, vld1q_u8(src + 32));
  if (len <= 48) {
    return;
  }
  vst1q_u8(dst + 48, vld1q_u8(src + 48));
}

// NFD normalize up to 16 bytes of UTF-8 using an end of code point mask.
// Returns the number of bytes consumed.
static size_t neon_normalize_masked_utf8_nfd(const uint8_t *input,
                                             uint64_t mask, uint8_t **out,
                                             bool *end_is_cc) {
  // Count trailing ones to get number of ASCII bytes at the start of input
  uint64_t rmask = __rbitll(mask);
  unsigned int t1 = __clzll(~rmask);
  // Skip as many ASCII bytes as possible. We eagerly skip ASCII because, even
  // if the number of ASCII bytes is small, benchmarks show that the cost of
  // falling into the slow path for a majority ASCII input vector is quite high,
  // especially for heavily Roman alphabetic languages broken up by occasional
  // diacritics, such as Spanish or French.
  if (t1 > 2) {
    if (*end_is_cc) {
      scalar_sort_characters(*out - 1);
    }
    size_t min = t1 > 52 ? 52 : t1;
    neon_memcpy_small(*out, input, min);
    *out += min;
    *end_is_cc = false;
    return min;
  }

  uint8x16_t in = vld1q_u8(input);
  uint16_t sml_mask = mask & 0xFFF;

  // Fast path for 4 3-byte code points
  if (sml_mask == 0x924) {
    uint16x4_t chars = neon_parse_three_byte_utf8(in);
    uint16_t min = vminv_u16(chars);
    uint16_t max = vmaxv_u16(chars);

    // The large, common 3-byte CJK range (encompasses much of CJK) that has
    // no precomposed code points. We can just skip these. Not a huge fan of
    // language-specific optimization like this (excluding ASCII), but the
    // speedups are so large for such a low cost that it seems worth it.
    if (min >= 0x30FF && max <= 0x9FFF) {
      neon_skip(in, 12, out, end_is_cc);
      return 12;
    }

    // Precomposed Hangul range. Characters in this range are algorithmically
    // decomposable with a few arithmetic operations. They are the only
    // precomposed characters we can decompose without a table lookup.
    //
    // Algorithm described here:
    // https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401
    if (min >= NORMDATA_S_BASE && max < NORMDATA_S_BASE + NORMDATA_S_COUNT) {
      if (*end_is_cc) {
        scalar_sort_characters(*out - 1);
      }
      // Hangul jamo are not combining characters
      *end_is_cc = false;

      uint16x4x3_t lvt = neon_compute_hangul_jamo(chars);

      // Only 12 of the 16 uint16_t's will be used
      uint16_t tmp[16];
      // Interleave store by three, creating a code point buffer assuming
      // all precomposed Hangul characters decompose into three Hangul
      // syllables each.
      vst3_u16(tmp, lvt);

      uint16x4_t t = vsub_u16(lvt.val[2], vdup_n_u16(NORMDATA_T_BASE));
      // Mask for all precomposed Hangul syllables that should not have a
      // trailing consonant
      uint16x4_t t_mask = vceqz_u16(t);
      uint8_t bitmask = neon_movemask_u16(t_mask);
      // Use the trailing consonant bitmask to get a shuffle vector
      NormdataHangulShuf shuf = NORMDATA_HANGUL_SHUF[bitmask];

      // Load the tmp buffer into a large byte table
      uint8x16_t tbl_low = vreinterpretq_u8_u16(vld1q_u16(tmp));
      uint8x16_t tbl_high = vreinterpretq_u8_u16(vld1q_u16(tmp + 8));
      uint8x16x2_t tbl;
      tbl.val[0] = tbl_low;
      tbl.val[1] = tbl_high;

      // Use the shuffle vector to reorder the syllables so that it (possibly)
      // corrects the previous code that assumed all characters decompose into
      // three syllables.
      //
      // NOTE: possible fast path: skip this if bitmask == 0b1111.
      uint8x16_t idx_low = vld1q_u8(shuf.tbl);
      uint16x8_t low = vreinterpretq_u16_u8(vqtbl2q_u8(tbl, idx_low));
      uint8x8_t idx_high = vld1_u8(shuf.tbl + 16);
      uint16x4_t high = vreinterpret_u16_u8(vqtbl2_u8(tbl, idx_high));

      neon_write_8_3_byte_utf8(low, *out);
      *out += 24;
      if (shuf.len > 24) {
        neon_write_8_3_byte_utf8(vcombine_u16(high, vdup_n_u16(0)), *out);
        *out += shuf.len - 24;
      }

      return 12;
    }

    // Fallback path for 4 3-byte characters
    uint32x4_t wide = vmovl_u16(chars);
    neon_decompose(in, wide, 12, input, out, end_is_cc);
    return 12;
  }

  // Four two-byte code points
  if ((sml_mask & 0xFF) == 0xAA) {
    uint16x4_t chars = neon_parse_2_byte_utf8(in);
    uint32x4_t wide = vmovl_u16(chars);
    uint32x4_t bloom = neon_evaluate_bloom(wide);
    // Hangul syllables are not possible here.
    if (vaddvq_u32(bloom) == 0) {
      neon_skip(in, 8, out, end_is_cc);
    } else {
      neon_decompose_non_hangul(wide, in, bloom, out, input, end_is_cc);
    }
    return 8;
  }

  uint8_t idx = NORMDATA_CODEPOINT_INDEX[sml_mask][0];
  uint8_t nchars = NORMDATA_CODEPOINT_INDEX[sml_mask][1];

  if (idx < NORMDATA_SHUFUTF8_INDEX_12) {
    // Four one to two byte code points
    uint16x4_t chars = neon_parse_4_12_utf8(in, idx);
    // Only the first four code points are used
    uint32x4_t wide = vmovl_u16(chars);
    uint32x4_t bloom = neon_evaluate_bloom(wide);
    // Hangul isn't possible here, so we don't need to check for it.
    if (vaddvq_u32(bloom) == 0) {
      neon_skip(in, nchars, out, end_is_cc);
    } else {
      neon_decompose_non_hangul(wide, in, bloom, out, input, end_is_cc);
    }
  } else if (idx < NORMDATA_SHUFUTF8_INDEX_123) {
    // Four code points
    uint16x4_t chars = neon_parse_4_123_utf8(in, idx);
    uint32x4_t wide = vmovl_u16(chars);
    neon_decompose(in, wide, nchars, input, out, end_is_cc);
  } else if (idx < NORMDATA_SHUFUTF8_INDEX_1234) {
    // TODO: right now, anytime we have 3 1..4-byte code points, we just fall
    //       back to scalar. This is because our functions are designed for
    //       4 code points, and we don't have a good way to handle the case
    //       where we have 3 code points. Four byte code points are uncommon
    //       enough in regular text that I think this path is okay, though.
    *out +=
        scalar_normalize_utf8_nfd_with_context(input, nchars, *out, end_is_cc);
  }

  return nchars;
}

static inline uint8x16_t neon_get_codepoint_starts(uint8x16_t in) {
  int8x16_t sgn = vreinterpretq_s8_u8(in);
  return vcltq_s8(sgn, vdupq_n_s8(-65 + 1));
}

static inline uint64_t neon_make_code_point_mask(uint8_t const *input) {
  uint8x16_t bit_mask = {0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80,
                         0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};
  uint8x16_t c0 = neon_get_codepoint_starts(vld1q_u8(input));
  uint8x16_t c1 = neon_get_codepoint_starts(vld1q_u8(input + 16));
  uint8x16_t c2 = neon_get_codepoint_starts(vld1q_u8(input + 32));
  uint8x16_t c3 = neon_get_codepoint_starts(vld1q_u8(input + 48));
  // Compute the 64-bit movemask
  uint8x16_t sum0 = vpaddq_u8(vandq_u8(c0, bit_mask), vandq_u8(c1, bit_mask));
  uint8x16_t sum1 = vpaddq_u8(vandq_u8(c2, bit_mask), vandq_u8(c3, bit_mask));
  sum0 = vpaddq_u8(sum0, sum1);
  sum0 = vpaddq_u8(sum0, sum0);

  uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(sum0), 0);
  mask = ~mask;
  // Shift right to get the end of each code point
  mask >>= 1;

  return mask;
}

size_t neon_normalize_utf8_nfd(uint8_t const *input, size_t length,
                               uint8_t *out) {
  uint8_t **out_ptr = &out;
  uint8_t *start = out;

  // It is possible that we do buffer overruns (but only _use_ the appropriate
  // number of bytes) in specifc cases. This margin makes sure that those
  // oversized store operations are safe.
  const size_t SAFETY_MARGIN = 16;
  bool end_is_cc = false;
  size_t p = 0;
  while (p + 64 + SAFETY_MARGIN <= length) {
    uint64_t mask = neon_make_code_point_mask(input + p);
    size_t pmax = (p + 64) - 12;
    while (p < pmax) {
      size_t consumed =
          neon_normalize_masked_utf8_nfd(input + p, mask, out_ptr, &end_is_cc);
      p += consumed;
      mask >>= consumed;
    }
  }

  if (p < length) {
    if (end_is_cc) {
      scalar_sort_characters(*out_ptr - 1);
    }

    // Write the rest using scalar code
    *out_ptr += scalar_normalize_utf8_nfd(input + p, length - p, *out_ptr);
  }

  return *out_ptr - start;
}

// amalgamate add: #endif // UTF8NORM_IMPLEMENTATION_NEON
