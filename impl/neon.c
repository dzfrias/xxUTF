// amalgamate add: #if UTF8NORM_IMPLEMENTATION_NEON

#include "impl/neon.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_acle.h>
#include <arm_neon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static inline uint8_t neon_movemask_u16(uint16x4_t v) {
  const uint16x4_t mask = {0x1, 0x2, 0x4, 0x8};
  uint16x4_t mv = vand_u16(v, mask);
  return (uint8_t)(vaddv_u16(mv) & 0xF);
}

#define NEON_PRINT_FUNC(type, child_type, store_func)                          \
  __attribute__((unused)) static void neon_print_##type(const char *name,      \
                                                        type vec) {            \
    child_type values[sizeof(type) / sizeof(child_type)];                      \
    store_func(values, vec);                                                   \
    printf("%s: ", name);                                                      \
    for (int i = 0; i < sizeof(values) / sizeof(child_type); i++) {            \
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

// Parse six two-byte UTF-8 code points into their 32-bit code point values.
// Taken from simdutf
static uint16x8_t neon_parse_2_byte_utf8(uint8x16_t in) {
  // Converts 6 2 byte UTF-8 characters to 6 UTF-16 characters.

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
  return composed;
}

// Parse six code points encoded in UTF-8 into 16-bit code point values.
// Taken from simdutf
static uint16x8_t neon_parse_6_utf8(uint8x16_t in, size_t shufutf8_idx) {
  // Converts 6 1-2 byte UTF-8 characters to 6 UTF-16 characters.
  // This is a relatively easy scenario
  // we process SIX (6) input code-code units. The max length in bytes of six
  // code code units spanning between 1 and 2 bytes each is 12 bytes.
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
  return composed;
}

// Parse three code points encoded in UTF-8 into 32-bit code point values.
// Taken from simdutf
static uint32x4_t neon_parse_3_utf8(uint8x16_t in, size_t shufutf8_idx) {
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

// Parse three four-byte UTF-8 code points into their 32-bit code point values.
// Taken from simdutf
static uint32x4_t neon_parse_4_byte_utf8(uint8x16_t in) {
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
static uint16x4_t neon_parse_4_utf8(uint8x16_t in, size_t shufutf8_idx) {
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

// Hash four 32-bit values using the perfect hashing function.
static uint32x4_t neon_phash(uint32x4_t key, uint32x4_t salt, uint32_t size) {
  uint32x4_t salt_key = vaddq_u32(key, salt);
  uint32x4_t y1 = vmulq_n_u32(salt_key, 2654435769);
  uint32x4_t y2 = vmulq_n_u32(key, 0x31415926);
  uint32x4_t y = veorq_u32(y1, y2);
  uint64x2_t mul_hash_low = vmull_n_u32(vget_low_u32(y), size);
  uint64x2_t mul_hash_high = vmull_high_n_u32(y, size);
  uint32x2_t shifted_low = vshrn_n_u64(mul_hash_low, 32);
  uint32x2_t shifted_high = vshrn_n_u64(mul_hash_high, 32);
  return vcombine_u32(shifted_low, shifted_high);
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

static bool neon_any_in_bloom_filter(uint32x4_t input) {
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
  return vaddvq_u32(result_eq) > 0;
}

__attribute__((unused)) static void neon_print_misses(uint32x4_t input) {
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

  uint32_t results[4];
  uint32_t chars[4];
  vst1q_u32(results, result_eq);
  vst1q_u32(chars, input);
  for (size_t i = 0; i < 4; i++) {
    if (results[i] > 0) {
      printf("miss: %0X\n", chars[i]);
    }
  }
}

static inline bool neon_any_hangul(uint32x4_t input) {
  uint32x4_t ge = vcgeq_u32(input, vdupq_n_u32(NORMDATA_S_BASE));
  uint32x4_t lt =
      vcltq_u32(input, vdupq_n_u32(NORMDATA_S_BASE + NORMDATA_S_COUNT));
  uint32x4_t cmp = vandq_u32(lt, ge);
  return vmaxvq_u32(cmp) > 0;
}

// Decompose up to four code points into their NFD form.
//
// `length` must be less than or equal to four. The mask should be a code
// point mask that corresponds to the input pointer. Returns the number of
// bytes written
__attribute__((unused)) static size_t
neon_decompose(uint32x4_t values, size_t length, uint16_t mask, uint8_t **out,
               const uint8_t *input, bool *end_is_cc) {
  uint32x4_t salt_hash =
      neon_phash(values, vdupq_n_u32(0), NORMDATA_DECOMPOSED_SALT_SIZE);
  // Lookups are done in a scalar manner
  uint32x4_t salt = {
      NORMDATA_DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 0)],
      NORMDATA_DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 1)],
      NORMDATA_DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 2)],
      NORMDATA_DECOMPOSED_SALT[vgetq_lane_u32(salt_hash, 3)],
  };
  uint32x4_t key_hash = neon_phash(values, salt, NORMDATA_DECOMPOSED_SALT_SIZE);
  uint32_t hashes[4];
  uint32_t chars[4];
  vst1q_u32(hashes, key_hash);
  vst1q_u32(chars, values);

  bool last_is_cc = *end_is_cc;
  size_t offset = 0;
  // We reverse the code point mask so we can count the number of trailing
  // zeroes (ARM only has clz).
  uint64_t rmask = __rbitll(mask);
  // Enter scalar code to do lookups
  for (int i = 0; i < length; i++) {
    bool is_cc = false;
    unsigned int tz = __clzll(rmask << offset);
    // Get the size of the code point using the number of trailing zeroes
    uint32_t size = tz + 1;
    uint8_t nwritten = size;

    // ASCII fast path
    if (chars[i] <= 0x7F) {
      *(*out)++ = chars[i];
      is_cc = false;
      goto loop_end;
    }

    // Hangul check
    if (scalar_is_hangul(chars[i])) {
      nwritten = scalar_decompose_hangul(chars[i], *out);
      *out += nwritten;
      is_cc = false;
      goto loop_end;
    }

    NormdataEntry kv = NORMDATA_DECOMPOSED_KV[hashes[i]];

    // Check if the character has a decomposition
    if (kv.k == chars[i]) {
      uint8_t const *start = &NORMDATA_DECOMPOSED_CHARS[kv.offset];
      for (uint8_t j = 0; j < kv.len; j++) {
        *(*out)++ = start[j];
      }
      is_cc = kv.ccc > 0;
      nwritten = kv.len;
    } else {
      uint8_t const *start = input + offset;
      for (uint8_t j = 0; j < size; j++) {
        *(*out)++ = start[j];
      }
      is_cc = false;
    }

  loop_end:
    offset += size;
    if (last_is_cc && !is_cc) {
      scalar_sort_characters(*out - nwritten - 1);
    }
    last_is_cc = is_cc;
  }

  *end_is_cc = last_is_cc;
  return offset;
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
  uint64_t rmask = __rbitll(mask);
  unsigned int t1 = __clzll(~rmask);
  // Skip as many ASCII bytes as possible
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
      if (*end_is_cc) {
        scalar_sort_characters(*out - 1);
      }
      vst1q_u8(*out, in);
      *out += 12;
      *end_is_cc = false;
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

      // Mask for all precomposed Hangul syllables that should not have a
      // trailing consonant
      uint16x4_t t_mask = vceqz_u16(t);
      uint8_t bitmask = neon_movemask_u16(t_mask);
      // Use the trailing consonant bitmask to get a shuffle vector
      NormdataHangulShuf shuf = NORMDATA_HANGUL_SHUF[bitmask];

      // Only 12 of the 16 uint16_t's will be used
      uint16_t tmp[16];
      uint16x4x3_t vals;
      vals.val[0] = vadd_u16(l, vdup_n_u16(NORMDATA_L_BASE));
      vals.val[1] = vadd_u16(v, vdup_n_u16(NORMDATA_V_BASE));
      vals.val[2] = vadd_u16(t, vdup_n_u16(NORMDATA_T_BASE));
      // Interleave store by three, creating a code point buffer assuming
      // all precomposed Hangul characters decompose into three Hangul
      // syllables each.
      vst3_u16(tmp, vals);

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

      *end_is_cc = false;
      return 12;
    }

    // Fallback path for 4 3-byte characters
    uint32x4_t wide = vmovl_u16(chars);
    if (!neon_any_in_bloom_filter(wide) && !neon_any_hangul(wide)) {
      if (*end_is_cc) {
        scalar_sort_characters(*out - 1);
      }
      vst1q_u8(*out, in);
      *out += 12;
      *end_is_cc = false;
      return 12;
    }
    *out += scalar_normalize_utf8_nfd_with_context(input, 12, *out, end_is_cc);
    return 12;
  }

  // Four two-byte code points
  if ((sml_mask & 0xFF) == 0xAA) {
    uint16x8_t chars = neon_parse_2_byte_utf8(in);
    // We only use the first four code points
    uint32x4_t lower = vmovl_u16(vget_low_u16(chars));
    if (!neon_any_in_bloom_filter(lower)) {
      if (*end_is_cc) {
        scalar_sort_characters(*out - 1);
      }
      vst1q_u8(*out, in);
      *out += 8;
      *end_is_cc = false;
      return 8;
    }
    *out += scalar_normalize_utf8_nfd_with_context(input, 8, *out, end_is_cc);
    return 8;
  }

  uint8_t idx = NORMDATA_CODEPOINT_INDEX[sml_mask][0];
  uint8_t nchars = NORMDATA_CODEPOINT_INDEX[sml_mask][1];

  // TODO: this case right now is "recognize six, only take four". Try cleaning
  //       it up into "recognize four, take four"
  if (idx < 64) {
    // Six one to two byte code points
    uint16x8_t chars = neon_parse_6_utf8(in, idx);
    // Only the first four code points are used
    uint32x4_t lower = vmovl_u16(vget_low_u16(chars));
    if (!neon_any_in_bloom_filter(lower)) {
      goto skip;
    }
    *out +=
        scalar_normalize_utf8_nfd_with_context(input, nchars, *out, end_is_cc);
    return nchars;
  } else if (idx < 145) {
    // Four code points
    uint16x4_t chars = neon_parse_4_utf8(in, idx);
    uint32x4_t wide = vmovl_u16(chars);
    if (!neon_any_in_bloom_filter(wide) && !neon_any_hangul(wide)) {
      goto skip;
    }
    *out +=
        scalar_normalize_utf8_nfd_with_context(input, nchars, *out, end_is_cc);
    return nchars;
  } else if (idx < 209) {
    // Three code points
    uint32x4_t chars;
    if (sml_mask == 0x888) {
      chars = neon_parse_4_byte_utf8(in);
    } else {
      chars = neon_parse_3_utf8(in, idx);
    }
    uint32x4_t mask = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0};
    uint32x4_t chars_masked = vandq_u32(chars, mask);
    if (!neon_any_in_bloom_filter(chars_masked) &&
        !neon_any_hangul(chars_masked)) {
      goto skip;
    }
    *out +=
        scalar_normalize_utf8_nfd_with_context(input, nchars, *out, end_is_cc);
    return nchars;
  } else {
    // This is an error
    return 12;
  }

skip:
  if (*end_is_cc) {
    scalar_sort_characters(*out - 1);
  }
  vst1q_u8(*out, in);
  *out += nchars;
  *end_is_cc = false;
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
  uint8x16_t sum0 = vpaddq_u8(vandq_u8(c0, bit_mask), vandq_u8(c1, bit_mask));
  uint8x16_t sum1 = vpaddq_u8(vandq_u8(c2, bit_mask), vandq_u8(c3, bit_mask));
  sum0 = vpaddq_u8(sum0, sum1);
  sum0 = vpaddq_u8(sum0, sum0);

  uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(sum0), 0);
  mask = ~mask;
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
  const size_t SAFETY_MARGIN = 12;
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
