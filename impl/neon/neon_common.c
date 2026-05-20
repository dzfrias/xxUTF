// amalgamate add: #if XXUTF_IMPLEMENTATION_NEON

#include "impl/neon/neon_common.h"
#include "normdata.h"
#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Macro to define a print function for an arbitrarily-shaped NEON vector.
#define NEON_PRINT_FUNC(type, child_type, store_func)                          \
  __attribute__((unused)) void neon_print_##type(const char *name, type vec) { \
    printf("%s: ", name);                                                      \
    for (uint8_t i = 0; i < sizeof(vec) / sizeof(child_type); i++) {           \
      printf("%04x ", vec[i]);                                                 \
    }                                                                          \
    printf("\n");                                                              \
  }

NEON_PRINT_FUNC(uint8x16_t, uint8_t, vst1q_u8);
NEON_PRINT_FUNC(uint8x8_t, uint8_t, vst1_u8);
NEON_PRINT_FUNC(uint16x8_t, uint16_t, vst1q_u16);
NEON_PRINT_FUNC(uint16x4_t, uint16_t, vst1_u16);
NEON_PRINT_FUNC(uint32x4_t, uint32_t, vst1q_u32);
NEON_PRINT_FUNC(uint32x2_t, uint32_t, vst1_u32);
NEON_PRINT_FUNC(int8x16_t, int8_t, vst1q_u8);
NEON_PRINT_FUNC(int8x8_t, int8_t, vst1_s8);
NEON_PRINT_FUNC(int16x8_t, int16_t, vst1q_s16);
NEON_PRINT_FUNC(int16x4_t, int16_t, vst1_s16);
NEON_PRINT_FUNC(int32x4_t, int32_t, vst1q_s32);
NEON_PRINT_FUNC(int32x2_t, int32_t, vst1_s32);

#undef NEON_PRINT_FUNC

uint16x4_t neon_hangul_mask(uint16x4_t input) {
  uint16x4_t ge = vcge_u16(input, vdup_n_u16(NORMDATA_S_BASE));
  uint16x4_t lt =
      vclt_u16(input, vdup_n_u16(NORMDATA_S_BASE + NORMDATA_S_COUNT));
  uint16x4_t cmp = vand_u16(lt, ge);
  return cmp;
}

uint16x4x3_t neon_compute_hangul_jamo(uint16x4_t chars) {
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

void neon_memcpy_small(uint8_t *dst, const uint8_t *src) {
  vst1q_u8(dst, vld1q_u8(src));
  vst1q_u8(dst + 16, vld1q_u8(src + 16));
  vst1q_u8(dst + 32, vld1q_u8(src + 32));
  vst1q_u8(dst + 48, vld1q_u8(src + 48));
}

uint64_t neon_bitmask4(uint8x16_t v) {
  uint16x8_t v16 = vreinterpretq_u16_u8(v);
  uint8x8_t res = vshrn_n_u16(v16, 4);
  return vget_lane_u64(vreinterpret_u64_u8(res), 0);
}

// Taken from simdutf
uint16x4_t neon_parse_3_byte_utf8(uint8x16_t in) {
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

// Taken from simdutf
uint16x4_t neon_parse_2_byte_utf8(uint8x16_t in) {
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

// Taken from simdutf
uint32x4_t neon_parse_4_byte_utf8(uint8x16_t in) {
  // We want to take 3 4-byte UTF-8 code units and turn them into 3 4-byte
  // UTF-32 code units. This uses the same method as the fixed 3 byte
  // version, reversing and shift left insert. However, there is no need for
  // a shuffle mask now, just rev16 and rev32.
  //
  // This version does not use the LUT, but 4 byte sequences are less common
  // and the overhead of the extra memory access is less important than the
  // early branch overhead in shorter sequences, so it comes last.

  // Swap pairs of bytes
  // 10dddddd|10cccccc|10bbbbbb|11110aaa
  // 10cccccc 10dddddd|11110aaa 10bbbbbb
  uint16x8_t swap1 = vreinterpretq_u16_u8(vrev16q_u8(in));
  // Shift left and insert
  // xxxxcccc ccdddddd|xxxxxxxa aabbbbbb
  uint16x8_t merge1 = vsliq_n_u16(swap1, vreinterpretq_u16_u8(in), 6);
  // Swap 16-bit lanes
  // xxxxcccc ccdddddd xxxxxxxa aabbbbbb
  // xxxxxxxa aabbbbbb xxxxcccc ccdddddd
  uint32x4_t swap2 = vreinterpretq_u32_u16(vrev32q_u16(merge1));
  // Shift insert again
  // xxxxxxxx xxxaaabb bbbbcccc ccdddddd
  uint32x4_t merge2 = vsliq_n_u32(swap2, vreinterpretq_u32_u16(merge1), 12);
  // Clear the garbage
  // 00000000 000aaabb bbbbcccc ccdddddd
  uint32x4_t composed = vandq_u32(merge2, vmovq_n_u32(0x1FFFFF));
  return composed;
}

// Taken from simdutf
uint16x4_t neon_parse_4_12_utf8(uint8x16_t in, size_t shufutf8_idx) {
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

// Taken from simdutf
uint16x4_t neon_parse_4_123_utf8(uint8x16_t in, size_t shufutf8_idx) {
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

// Taken from simdutf
uint32x4_t neon_parse_3_1234_utf8(uint8x16_t in, size_t shufutf8_idx) {
  // Unlike UTF-16, doing a fast codepath doesn't have nearly as much benefit
  // due to surrogates no longer being involved.
  uint8x16_t sh = vld1q_u8(NORMDATA_SHUFUTF8[shufutf8_idx]);
  // 1 byte: 00000000 00000000 00000000 0ddddddd
  // 2 byte: 00000000 00000000 110ccccc 10dddddd
  // 3 byte: 00000000 1110bbbb 10cccccc 10dddddd
  // 4 byte: 11110aaa 10bbbbbb 10cccccc 10dddddd
  uint32x4_t perm = vreinterpretq_u32_u8(vqtbl1q_u8(in, sh));
  // Ascii
  uint32x4_t ascii = vandq_u32(perm, vmovq_n_u32(0x7F));
  uint32x4_t middle = vandq_u32(perm, vmovq_n_u32(0x3f00));
  // When converting the way we do, the 3 byte prefix will be interpreted as
  // the 18th bit being set, since the code would interpret the lead byte
  // (0b1110bbbb) as a continuation byte (0b10bbbbbb). To fix this, we can
  // either xor or do an 8 bit add of the 6th bit shifted right by 1. Since
  // NEON has shift right accumulate, we use that.
  //  4 byte   3 byte
  // 10bbbbbb 1110bbbb
  // 00000000 01000000 6th bit
  // 00000000 00100000 shift right
  // 10bbbbbb 0000bbbb add
  // 00bbbbbb 0000bbbb mask
  uint8x16_t correction =
      vreinterpretq_u8_u32(vandq_u32(perm, vmovq_n_u32(0x00400000)));
  uint32x4_t corrected = vreinterpretq_u32_u8(
      vsraq_n_u8(vreinterpretq_u8_u32(perm), correction, 1));
  // 00000000 00000000 0000cccc ccdddddd
  uint32x4_t cd = vsraq_n_u32(ascii, middle, 2);
  // Insert twice
  // xxxxxxxx xxxaaabb bbbbxxxx xxxxxxxx
  uint32x4_t ab = vbslq_u32(vmovq_n_u32(0x01C0000), vshrq_n_u32(corrected, 6),
                            vshrq_n_u32(corrected, 4));
  // 00000000 000aaabb bbbbcccc ccdddddd
  uint32x4_t composed = vbslq_u32(vmovq_n_u32(0xFFE00FFF), cd, ab);
  return composed;
}

uint8x16_t neon_get_utf8_code_point_starts(uint8x16_t in) {
  int8x16_t sgn = vreinterpretq_s8_u8(in);
  return vcltq_s8(sgn, vdupq_n_s8(-65 + 1));
}

uint64_t neon_make_utf8_code_point_mask(const uint8_t *input) {
  uint8x16_t bit_mask = {0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80,
                         0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};
  uint8x16_t c0 = neon_get_utf8_code_point_starts(vld1q_u8(input));
  uint8x16_t c1 = neon_get_utf8_code_point_starts(vld1q_u8(input + 16));
  uint8x16_t c2 = neon_get_utf8_code_point_starts(vld1q_u8(input + 32));
  uint8x16_t c3 = neon_get_utf8_code_point_starts(vld1q_u8(input + 48));
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

// Create a logical vector for high surrogates.
uint16x8_t neon_make_utf16_surrogates_mask(uint16x8_t in) {
  return vandq_u16(vcleq_u16(in, vdupq_n_u16(0xDBFF)),
                   vcgeq_u16(in, vdupq_n_u16(0xD800)));
}

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
