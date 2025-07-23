#include "impl/neon.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_acle.h>
#include <arm_neon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline uint16_t make_bitmask(uint8x16_t v) {
  const uint8x16_t mask = {0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80,
                           0x01, 0x02, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80};
  uint8x16_t mv = vandq_u8(v, mask);
  uint8x16_t tmp = vpaddq_u8(mv, mv);
  tmp = vpaddq_u8(tmp, tmp);
  tmp = vpaddq_u8(tmp, tmp);
  return vgetq_lane_u16(vreinterpretq_u16_u8(tmp), 0);
}

__attribute__((unused)) static void print_uint8x16(const char *name,
                                                   uint8x16_t vec) {
  uint8_t values[16];
  vst1q_u8(values, vec);

  printf("%s: ", name);
  for (int i = 0; i < 16; i++) {
    printf("%02x ", values[i]);
  }
  printf("\n");
}

__attribute__((unused)) static void print_uint16x8(const char *name,
                                                   uint16x8_t vec) {
  uint16_t values[8];
  vst1q_u16(values, vec);

  printf("%s: ", name);
  for (int i = 0; i < 8; i++) {
    printf("%04x ", values[i]);
  }
  printf("\n");
}

__attribute__((unused)) static void print_uint16x4(const char *name,
                                                   uint16x4_t vec) {
  uint16_t values[4];
  vst1_u16(values, vec);

  printf("%s: ", name);
  for (int i = 0; i < 4; i++) {
    printf("%04x ", values[i]);
  }
  printf("\n");
}

__attribute__((unused)) static void print_uint32x4(const char *name,
                                                   uint32x4_t vec) {
  uint32_t values[4];
  vst1q_u32(values, vec);

  printf("%s: ", name);
  for (int i = 0; i < 4; i++) {
    printf("%08x ", values[i]);
  }
  printf("\n");
}

// Parse four three-byte UTF-8 code points into their 16-bit code point values.
// Taken from simdutf
static uint16x4_t parse_three_byte_utf8(uint8x16_t in) {
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

// Parse six code points encoded in UTF-8 into 16-bit code point values.
// Taken from simdutf
static uint16x8_t parse_6_utf8(uint8x16_t in, size_t shufutf8_idx) {
  // Converts 6 1-2 byte UTF-8 characters to 6 UTF-16 characters.
  // This is a relatively easy scenario
  // we process SIX (6) input code-code units. The max length in bytes of six
  // code code units spanning between 1 and 2 bytes each is 12 bytes.
  uint8x16_t sh = vld1q_u8(SHUFUTF8[shufutf8_idx]);
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
  return composed;
}

// Parse three code points encoded in UTF-8 into 32-bit code point values.
// Taken from simdutf
static uint32x4_t parse_3_utf8(uint8x16_t in, size_t shufutf8_idx) {
  // Unlike UTF-16, doing a fast codepath doesn't have nearly as much benefit
  // due to surrogates no longer being involved.
  uint8x16_t sh = vld1q_u8(SHUFUTF8[shufutf8_idx]);
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

// Parse three four-byte UTF-8 code points into their 32-bit code point values.
// Taken from simdutf
static uint32x4_t parse_4_byte_utf8(uint8x16_t in) {
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

// Parse four code points encoded in UTF-8 into 16-bit code point values.
// Taken from simdutf
static uint16x4_t parse_4_utf8(uint8x16_t in, size_t shufutf8_idx) {
  // UTF-16 and UTF-32 use similar algorithms, but UTF-32 skips the narrowing.
  uint8x16_t sh = vld1q_u8(SHUFUTF8[shufutf8_idx]);
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

static inline bool in_range_u16(uint16x4_t res, uint16_t a, uint16_t b) {
  uint16x4_t lt = vclt_u16(res, vdup_n_u16(b));
  uint16x4_t gt = vcgt_u16(res, vdup_n_u16(a));
  uint16x4_t cmp = vand_u8(lt, gt);
  return vminv_u16(cmp) == 0xFFFF;
}

// Hash four 32-bit values using the perfect hashing function.
static uint32x4_t phash(uint32x4_t key, uint32x4_t salt, uint32_t size) {
  uint32x4_t salt_key = vaddq_u32(key, salt);
  uint32x4_t y1 = vmulq_u32(salt_key, vdupq_n_u32(2654435769));
  uint32x4_t y2 = vmulq_u32(key, vdupq_n_u32(0x31415926));
  uint32x4_t y = veorq_u32(y1, y2);
  uint64x2_t mul_hash_low = vmull_u32(vget_low_u32(y), vdup_n_u32(size));
  uint64x2_t mul_hash_high = vmull_high_u32(y, vdupq_n_u32(size));
  uint32x2_t shifted_low = vshrn_n_u64(mul_hash_low, 32);
  uint32x2_t shifted_high = vshrn_n_u64(mul_hash_high, 32);
  return vcombine_u32(shifted_low, shifted_high);
}

// Decompose up to four code points into their NFD form.
//
// `length` must be less than or equal to four. The mask should be a code point
// mask that corresponds to the input pointer.
static size_t decompose(uint32x4_t values, size_t length, uint16_t mask,
                        char **out, const char *input) {
  static const uint32_t SALT_SIZE = sizeof(DECOMPOSED_SALT) / 2;

  uint32x4_t salt_hash = phash(values, vdupq_n_u32(0), SALT_SIZE);
  uint32x4_t salt = {
      DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 0)],
      DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 1)],
      DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 2)],
      DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 3)],
  };
  uint32x4_t key_hash = phash(values, salt, SALT_SIZE);
  uint32_t hashes[4];
  uint32_t chars[4];
  vst1q_u32(hashes, key_hash);
  vst1q_u32(chars, values);

  size_t offset = 0;
  // We reverse the code point mask so we can count the number of trailing
  // zeroes (ARM only has clz).
  uint64_t rmask = __rbitll(mask);
  // Enter scalar code to do lookups
  for (int i = 0; i < length; i++) {
    Entry kv = DECOMPOSED_KV[hashes[i]];
    unsigned int tz = __clzll(rmask << offset);
    // Get the size of the code point using the number of trailing zeroes
    uint32_t size = tz + 1;

    // Check if the character has a decomposition
    if (kv.k == chars[i]) {
      uint8_t const *start = &DECOMPOSED_CHARS[kv.offset];
      memcpy(*out, start, kv.len);
      (*out) += kv.len;
    } else {
      memcpy((*out), input + offset, size);
      (*out) += size;
    }
    offset += size;
  }

  return offset;
}

// NFD normalize up to 16 bytes of UTF-8 using an end of code point mask.
// Returns the number of bytes consumed.
static size_t normalize_masked_utf8_nfd(const char *input, uint64_t mask,
                                        char **out) {
  uint32x4_t in = vld1q_u8((uint8_t const *)input);

  if ((mask & 0xFFFF) == 0xFFFF) {
    printf("ALL ASCII\n");
    vst1q_u8((unsigned char *)(*out), in);
    (*out) += 16;
    return 16;
  }

  uint16_t sml_mask = mask & 0xFFF;

  // Fast path for 4 3-byte code points (common in CJK)
  if (sml_mask == 0x924) {
    printf("FOUND THREE BYTE\n");

    uint16x4_t cps = parse_three_byte_utf8(in);

    // The largest 3-byte CJK range (encompasses the vast, vast majority of CJK)
    // that has no precomposed code points. We can just skip these.
    if (in_range_u16(cps, 0x30FF - 1, 0x9FFF + 1)) {
      vst1q_u8((unsigned char *)(*out), in);
      (*out) += 12;
      return 12;
    }

    // Precomposed Hangul range. Characters in this range are algorithmically
    // decomposable with a few arithmetic operations. They are the only
    // precomposed characters we can decompose without a table lookup.
    //
    // Algorithm described here:
    // https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401
    if (in_range_u16(cps, 0xAC00 - 1, 0xD7AF + 1)) {
      // printf("FOUND HANGUL\n");
      //
      // // Compute the S index
      // uint16x4_t s = vsub_u16(cps, vdup_n_u16(S_BASE));
      //
      // // Compute the l index: s / N_COUNT
      // uint32x4_t l_fixed = vmull_u32(s, vdup_n_u16(28533));
      // // Shift the fixed point number
      // uint32x4_t l_wide = vshrq_n_u32(l_fixed, 24);
      // uint16x4_t l = vmovn_u32(l_wide);
      //
      // // Multiply and subtract to get the remainder
      // uint16x4_t v_modulo = vmls_u16(l, vdup_n_u16(N_COUNT), s);
      // uint16x4_t v_shifted = vshr_n_u16(v_modulo, 2);
      // uint32x4_t v_fixed = vmull_u16(v_shifted, vdup_n_u16(18725));
      // uint32x4_t v_wide = vshrq_n_u32(v_fixed, 17);
      // uint16x4_t v = vmovn_u32(v_wide);
      //
      // uint16x4_t t_shifted = vshr_n_u16(s, 2);
      // uint32x4_t t_fixed = vmull_u16(t_shifted, vdup_n_u16(18725));
      // // s / T_COUNT
      // uint32x4_t t_div_wide = vshrq_n_u32(t_fixed, 17);
      // uint16x4_t t_div = vmovn_u32(t_div_wide);
      // uint16x4_t t = vmls_u16(t_div, vdup_n_u16(T_COUNT), s);

      // Mask for all precomposed Hangul syllables that should not have a
      // trailing consonant
      // uint16x4_t t_mask = vceqz_u16(t);

      // TODO: modify this to be the correct bitmask
      // This allows us to compute a bitmask in two instructions
      /*const uint16x8_t bitmask = { 0x0101 , 0x0202, 0x0404, 0x0808, 0x1010,
       * 0x2020, 0x4040, 0x8080 };*/
      /*uint16x8_t mt = vandq_u16(t_mask, bitmask);*/
      /*uint16_t t_bitmask = vaddvq_u16(mt);*/

      // TODO: compute t index
      // to solve the t index byte placement problem:
      // create a logical vector that describes which bytes have t indices.
      // Turn into a bitmask (4 bits wide). Look up into a table that returns
      // a [4]u8 of where to put each L-index code point (i.e. [0, 2, 5, 7]).
      // From there can derive where to put V-index code point and T-index code
      // point
    }
  }

  uint8_t idx = CODEPOINT_INDEX[sml_mask][0];
  uint8_t nchars = CODEPOINT_INDEX[sml_mask][1];

  if (idx < 64) {
    // Six one to two byte code points
    uint16x8_t chars = parse_6_utf8(in, idx);
    uint32x4_t lower = vmovl_u16(vget_low_u16(chars));
    size_t consumed = decompose(lower, 4, sml_mask, out, input);
    uint16_t new_mask = sml_mask >> consumed;
    uint32x4_t upper = vmovl_u16(vget_high_u16(chars));
    (void)decompose(upper, 2, new_mask, out, input + consumed);
  } else if (idx < 145) {
    // Four code points
    uint16x4_t chars = parse_4_utf8(in, idx);
    uint32x4_t wide = vmovl_u16(chars);
    (void)decompose(wide, 4, sml_mask, out, input);
  } else if (idx < 209) {
    // Three code points
    if (sml_mask == 0x888) {
      uint32x4_t chars = parse_4_byte_utf8(in);
      (void)decompose(chars, 3, sml_mask, out, input);
    } else {
      uint32x4_t chars = parse_3_utf8(in, idx);
      (void)decompose(chars, 3, sml_mask, out, input);
    }
  } else {
    return 12;
  }

  return nchars;
}

static inline uint8x16_t get_codepoint_starts(uint8x16_t in) {
  int8x16_t sgn = vreinterpretq_s8_u8(in);
  return vcltq_s8(sgn, vdupq_n_s8(-65 + 1));
}

size_t normalize_utf8_nfd_neon(char const *input, size_t length, char *out) {
  char **out_ptr = &out;
  char *start = out;

  size_t p = 0;
  while (p + 64 <= length) {
    uint8x16_t in1 = vld1q_u8((uint8_t const *)(input + p));
    uint8x16_t in2 = vld1q_u8((uint8_t const *)(input + p + 16));
    uint8x16_t in3 = vld1q_u8((uint8_t const *)(input + p + 32));
    uint8x16_t in4 = vld1q_u8((uint8_t const *)(input + p + 48));
    uint64_t start1 = make_bitmask(get_codepoint_starts(in1));
    uint64_t start2 = make_bitmask(get_codepoint_starts(in2));
    uint64_t start3 = make_bitmask(get_codepoint_starts(in3));
    uint64_t start4 = make_bitmask(get_codepoint_starts(in4));

    uint64_t mask = start1 | (start2 << 16) | (start3 << 32) | (start4 << 48);
    mask = ~mask;
    mask >>= 1;
    size_t pmax = (p + 64) - 12;
    while (p <= pmax) {
      size_t consumed = normalize_masked_utf8_nfd(input + p, mask, out_ptr);
      p += consumed;
      mask >>= consumed;
    }
  }

  if (p < length) {
    // Write the rest using scalar code
    (*out_ptr) += normalize_utf8_nfd_scalar(input + p, length - p, *out_ptr);
  }

  return (*out_ptr) - start;
}
