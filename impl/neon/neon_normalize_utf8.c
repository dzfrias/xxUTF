// amalgamate add: #if XXUTF_IMPLEMENTATION_NEON

#include "impl/neon.h"
#include "impl/neon/neon_common.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_neon.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Create an 8-bit movemask from a 16x4 vector.
static uint8_t neon_movemask_u16(uint16x4_t v) {
  const uint16x4_t mask = {0x1, 0x2, 0x4, 0x8};
  uint16x4_t mv = vand_u16(v, mask);
  return (uint8_t)(vaddv_u16(mv) & 0xF);
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

// Write a 3-byte code point into the output buffer. The code point is assumed
// to be in the range of 0x0800 to 0xFFFF.
static void neon_write_3_byte_code_point_utf8(uint16_t code_point,
                                              uint8_t *out) {
  out[0] = 0xE0 | (code_point >> 12);
  out[1] = 0x80 | ((code_point >> 6) & 0x3F);
  out[2] = 0x80 | (code_point & 0x3F);
}

// Decompose a 4x32-bit vector of code points into their UTF-8 representations,
// writing them into the output buffer. The relevant mask indicates which code
// points should be decomposed as Hangul (0 meaning irrelevant). The irrelevant
// code points are copied as-is from the input buffer.
//
// This function assumes that the input code points are Hangul syllables.
static void neon_decompose_hangul_utf8(uint16x4_t chars, uint16x4_t relevant,
                                       uint8_t **out, const uint8_t *input,
                                       uint8_t *last_ccc) {
  *last_ccc = 0;
  // Decompose everthing (assuming they're all Hangul syllables). This
  // assumption is made because, empirically, most of the time this function is
  // called, it is because there is a single ASCII space in the input vector,
  // which causes a branch miss for the pure Hangul code path, and puts is in
  // this path. Therefore, we can still eagerly compute the Hangul jamo values,
  // and then only write the relevant ones.
  uint16x4x3_t lvt = neon_compute_hangul_jamo(chars);

#pragma clang loop unroll(enable)
  for (size_t i = 0; i < 4; i++) {
    if (input[0] <= 0x7F) {
      *(*out)++ = input[0];
      input++;
      continue;
    }
    if (relevant[i] == 0) {
      // Not a Hangul syllable, just copy the input.
      size_t size = NORMDATA_UTF8_SIZE[input[0]];
      for (size_t j = 0; j < size; j++) {
        *(*out)++ = input[j];
      }
      input += size;
      continue;
    }

    uint16_t l = lvt.val[0][i];
    uint16_t v = lvt.val[1][i];
    uint16_t t = lvt.val[2][i];

    neon_write_3_byte_code_point_utf8(l, *out);
    *out += 3;
    neon_write_3_byte_code_point_utf8(v, *out);
    *out += 3;
    // Naively write the T code point, even if it is zero, and branchlessly
    // increment the output pointer if it is non-zero. Although this appears
    // like extra work, it is actually faster than branching on the T code point
    // being zero or not, because the branch miss penalty is quite high. For the
    // korean.txt benchmark, this gave a ~33% speedup.
    neon_write_3_byte_code_point_utf8(t, *out);
    *out += 3 * (t - NORMDATA_T_BASE > 0);
    input += 3;
  }
}

static void neon_decompose_all_hangul_utf8(uint16x4_t values, uint8_t **out,
                                           uint8_t *last_ccc) {
  // Hangul jamo are not combining characters
  *last_ccc = 0;

  uint16x4x3_t lvt = neon_compute_hangul_jamo(values);

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
}

static inline uint16x8_t neon_prefix_sum_uint16x8(uint16x8_t v) {
  uint16x8_t t;
  t = vextq_u16(vdupq_n_u16(0), v, 7);
  v = vaddq_u16(v, t);
  t = vextq_u16(vdupq_n_u16(0), v, 6);
  v = vaddq_u16(v, t);
  t = vextq_u16(vdupq_n_u16(0), v, 4);
  v = vaddq_u16(v, t);
  return v;
}

static inline uint64_t neon_bitmask4(uint8x16_t v) {
  uint16x8_t v16 = vreinterpretq_u16_u8(v);
  uint8x8_t res = vshrn_n_u16(v16, 4);
  return vget_lane_u64(vreinterpret_u64_u8(res), 0);
}

// Taken from simdutf
static uint16x8_t neon_parse_2_byte_utf8_wide(uint8x16_t in) {
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

// Taken from simdutf
static uint16x8_t neon_parse_4_12_utf8_wide(uint8x16_t in,
                                            size_t shufutf8_idx) {
  uint8x16_t sh = vld1q_u8(NORMDATA_SHUFUTF8_WIDE[shufutf8_idx]);
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

// Taken from simdutf
static uint16x4_t neon_parse_4_123_utf8_wide(uint8x16_t in,
                                             size_t shufutf8_idx) {
  // UTF-16 and UTF-32 use similar algorithms, but UTF-32 skips the narrowing.
  uint8x16_t sh = vld1q_u8(NORMDATA_SHUFUTF8_WIDE[shufutf8_idx]);
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

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

#define NEON_DEFINE_NORMALIZE_FUNCTIONS(decomp_form, decomp_form_upper,             \
                                        comp_form, comp_form_upper,                 \
                                        large_decompositions)                       \
  /* Decompose up to eight code points into their UTF-8 representations.            \
   *                                                                                \
   * This function assumes that the input code points are not Hangul                \
   * syllables. */                                                                  \
  static void neon_write_non_hangul_utf8_##decomp_form##_fallback(                  \
      uint16x8_t values, uint16x8_t chars, uint8_t n_chars, uint8_t **out,          \
      size_t out_length, const uint8_t *input, uint8_t *last_ccc) {                 \
    uint8_t *start = *out;                                                          \
                                                                                    \
    /* TODO: we could make this faster by removing this parameter and inlining      \
     *       it instead... */                                                       \
    for (size_t i = 0; i < n_chars; i++) {                                          \
      uint8_t leading = input[0];                                                   \
      if (leading <= 0x7F) {                                                        \
        /* ASCII code point, no decomposition needed. */                            \
        *(*out)++ = leading;                                                        \
        input++;                                                                    \
        *last_ccc = 0;                                                              \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      uint16_t value = values[i];                                                   \
      uint8_t size = NORMDATA_UTF8_SIZE[leading];                                   \
      if (value <= 3) {                                                             \
        vst1_u8(*out, vld1_u8(input));                                              \
        *out += size;                                                               \
        input += size;                                                              \
        *last_ccc = 0;                                                              \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      /* `ccc` represents the combining class of the last character in the          \
       * decomposition of the character we're on, not the actual ccc value of       \
       * the character. */                                                          \
      uint8_t ccc = (value >> 2) & 0xFF;                                            \
                                                                                    \
      uint16_t data_index =                                                         \
          NORMDATA_UTF8_##decomp_form_upper##_DATA_TRIE_INDEX[chars[i] >> 6];       \
      uint32_t data =                                                               \
          NORMDATA_UTF8_##decomp_form_upper##_DATA_TRIE_DATA[data_index +           \
                                                             (chars[i] & 63)];      \
      uint16_t offset = data & 0xFFFF;                                              \
      uint8_t length = (data >> 16) & 0xFF;                                         \
      uint8_t first_ccc = data >> 24;                                               \
                                                                                    \
      const uint8_t *decomp_offset =                                                \
          &NORMDATA_UTF8_##decomp_form_upper##_TRIE_DECOMPOSITIONS[offset];         \
      vst1q_u8(*out, vld1q_u8(decomp_offset));                                      \
      /* `large_decompositions` is a preprocessor-known value, so the compiler      \
       * will optimize this check out if it is false. In NFD, we only need to       \
       * copy a maximum of 16 bytes when writing a given character's                \
       * decomposition. But NFKD decompositions can get very large (check out       \
       * 0xFDFA!). */                                                               \
      if (large_decompositions && unlikely(length > 16)) {                          \
        vst1q_u8(*out + 16, vld1q_u8(decomp_offset + 16));                          \
        for (size_t j = 32; j < length; j++) {                                      \
          (*out)[j] = decomp_offset[j];                                             \
        }                                                                           \
      }                                                                             \
      *out += length;                                                               \
                                                                                    \
      uint8_t cmp_ccc = first_ccc > 0 ? first_ccc : ccc;                            \
      if (cmp_ccc != 0 && *last_ccc > cmp_ccc) {                                    \
        ccc = scalar_sort_characters_utf8(*out, out_length + (*out - start));       \
      }                                                                             \
      input += size;                                                                \
      *last_ccc = ccc;                                                              \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* Decompose code points into their UTF-8 representations.                        \
   *                                                                                \
   * This function assumes that the input code points are not Hangul                \
   * syllables and that they are "simple". In particular, this means the total      \
   * decomposition of the (up to) six `chars` code points cannot exceed 16          \
   * bytes in size. Also, no combining characters can be out of order. */           \
  __attribute__((always_inline)) static inline void                                 \
      neon_write_non_hangul_simple_utf8_##decomp_form(                              \
          uint8x16_t in, uint16x8_t chars, int16x8_t delta, uint16x8_t values,      \
          uint8_t *out) {                                                           \
    /* UTF-8 lengths of each code point in the input */                             \
    uint16x8_t length = vandq_u16(values, vdupq_n_u16(0b11));                       \
    uint16x8_t length_psum = neon_prefix_sum_uint16x8(length);                      \
    int8x16_t shift = vdupq_n_u8(0);                                                \
    int8x16_t iota = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};        \
    /* Each 8 byte block corresponds to one decomposition in the input. It is       \
     * only possible to have a maximum of six code points. */                       \
    uint8_t tbls[6 * 8];                                                            \
    /* Mask for all values with decompositions */                                   \
    uint16x8_t decomps = vcgtq_u16(values, vdupq_n_u16(0b11));                      \
    /* We can iterate through this bitmask to get the positions of all code         \
     * points we should decompose */                                                \
    uint64_t bitmask8 =                                                             \
        neon_bitmask4(vreinterpretq_u8_u16(decomps)) & 0x8080808080808080ULL;       \
    /* Keeps track of which sub-table we're writing to */                           \
    uint8_t j = 0;                                                                  \
    /* We could use a prefix sum involving `delta` to compute displacement as       \
     * a vector, but going scalar here is better because the loop below             \
     * usually executes only one or two times. */                                   \
    int8_t displacement = 0;                                                        \
    /* Iterate through 1 bits of `bitmask8` */                                      \
    /* TODO: I'd like a way to create the `shift` vector with less                  \
     *       instructions per decomposable code point  */                           \
    for (; bitmask8 > 0; bitmask8 &= bitmask8 - 1) {                                \
      /* We have 7 redundant bits per useful 1 bit  (that were masked out           \
       * earlier but still exist), so divide them out here */                       \
      uint32_t i = __builtin_ctzll(bitmask8) >> 3;                                  \
      int8_t dlt = delta[i];                                                        \
      int8_t size = length[i];                                                      \
      /* The end of each code point before displacement */                          \
      int8_t end = length_psum[i];                                                  \
      assert(end > 0);                                                              \
      /* The start of each code point after displacement */                         \
      int8_t dlt_start = (end - size) + displacement;                               \
      assert(dlt_start >= 0);                                                       \
      /* To decompose the code point at `i`, we need to shift the bytes in the      \
       * buffer by the amount the original code point expands during                \
       * decomposition. This mask tells us which bytes to shift. */                 \
      uint8x16_t shift_mask =                                                       \
          vcgeq_s8(iota, vdupq_n_s8(dlt_start + size + dlt));                       \
      uint8x16_t upper_mask =                                                       \
          vcltq_s8(iota, vdupq_n_s8(end + displacement + dlt));                     \
      uint8x16_t lower_mask = vcgeq_s8(iota, vdupq_n_s8(dlt_start));                \
      uint8x16_t decomp_mask = vandq_u8(upper_mask, lower_mask);                    \
      /* Shift by `dlt` */                                                          \
      int8x16_t contrib = vandq_s8(shift_mask, vdupq_n_s8(dlt));                    \
      int8_t offset_diff = -((int16_t)((j + 2) * 8) - dlt_start);                   \
      /* Performing `iota - tbl_offset` should get us an index into the             \
       * appropriate section of `tbls`, given by the formula. */                    \
      int8x16_t tbl_offset = vdupq_n_s8(offset_diff);                               \
      /* For our decomposition, select from `tbl_offset`, not the shift. */         \
      shift = vbslq_s8(decomp_mask, tbl_offset, vaddq_s8(shift, contrib));          \
      uint16_t code_point = chars[i];                                               \
      /* Pre-load the decomposition into the appropriate sub-table */               \
      uint16_t data_index =                                                         \
          NORMDATA_UTF8_##decomp_form_upper##_DATA_TRIE_INDEX[code_point >>         \
                                                              6];                   \
      uint32_t value = NORMDATA_UTF8_##decomp_form_upper##_DATA_TRIE_DATA           \
          [data_index + (code_point & 63)];                                         \
      vst1_u8(&tbls[j * 8],                                                         \
              vld1_u8(&NORMDATA_UTF8_##decomp_form_upper##_TRIE_DECOMPOSITIONS      \
                          [value & 0xFFFF]));                                       \
      j++;                                                                          \
      displacement += dlt;                                                          \
    }                                                                               \
    uint8x16x4_t tbl = {in, vld1q_u8(&tbls[0]), vld1q_u8(&tbls[16]),                \
                        vld1q_u8(&tbls[32])};                                       \
    uint8x16_t index = vreinterpretq_u8_s8(vsubq_s8(iota, shift));                  \
    uint8x16_t decomposed = vqtbl4q_u8(tbl, index);                                 \
    vst1q_u8(out, decomposed);                                                      \
  }                                                                                 \
                                                                                    \
  /* Decompose input code points, assuming they are not precomposed Hangul          \
   * syllables. `n_bytes` is the number of bytes that the six `chars` code          \
   * points occupy within the 16 byte `in` vector. */                               \
  __attribute__((always_inline)) static inline void                                 \
      neon_decompose_non_hangul_utf8_##decomp_form(                                 \
          uint8x16_t in, uint16x8_t chars, size_t n_bytes,                          \
          const uint8_t *input, uint8_t **out, size_t out_length,                   \
          uint8_t *last_ccc) {                                                      \
    uint16x8_t index = vshrq_n_u16(chars, 6);                                       \
    uint16x8_t block_index = {                                                      \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vgetq_lane_u16(index,        \
                                                                      0)],          \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vgetq_lane_u16(index,        \
                                                                      1)],          \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vgetq_lane_u16(index,        \
                                                                      2)],          \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vgetq_lane_u16(index,        \
                                                                      3)],          \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vgetq_lane_u16(index,        \
                                                                      4)],          \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vgetq_lane_u16(index,        \
                                                                      5)],          \
        0,                                                                          \
        0,                                                                          \
    };                                                                              \
    uint16x8_t masked = vandq_u16(chars, vdupq_n_u16(0x3F));                        \
    uint16x8_t data_offset = vaddq_u16(block_index, masked);                        \
    uint16x8_t values = {                                                           \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vgetq_lane_u16(               \
            data_offset, 0)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vgetq_lane_u16(               \
            data_offset, 1)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vgetq_lane_u16(               \
            data_offset, 2)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vgetq_lane_u16(               \
            data_offset, 3)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vgetq_lane_u16(               \
            data_offset, 4)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vgetq_lane_u16(               \
            data_offset, 5)],                                                       \
        0,                                                                          \
        0,                                                                          \
    };                                                                              \
    /* Each value contains the UTF-8 code point size (from 0 to 3) in the           \
     * lowest two bits. If there's nothing special about the code point, it         \
     * will have zero bits past those two bits. */                                  \
    if (vmaxvq_u16(values) <= 0b11) {                                               \
      *last_ccc = 0;                                                                \
      vst1q_u8(*out, in);                                                           \
      *out += n_bytes;                                                              \
      return;                                                                       \
    }                                                                               \
    int16x8_t delta = vshrq_n_s16(vreinterpretq_s16_u16(values), 11);               \
    int16_t total = (int16_t)n_bytes + vaddvq_s16(delta);                           \
    assert(total > 0);                                                              \
    uint16x8_t ccc_values =                                                         \
        vandq_u16(vshrq_n_u16(values, 2), vdupq_n_u16(0xFF));                       \
    uint16x8_t shifted_ccc = vextq_u16(vdupq_n_u16(*last_ccc), ccc_values, 7);      \
    uint16x8_t starters = vceqq_u16(ccc_values, vdupq_n_u16(0));                    \
    /* We can use the special ccc value 255 for starters */                         \
    uint16x8_t ccc_fixup = vbslq_u16(starters, vdupq_n_u16(255), ccc_values);       \
    uint16x8_t ccc_lt = vcltq_u16(ccc_fixup, shifted_ccc);                          \
    /* There are two conditions in which we would enter the slow path:              \
     *                                                                              \
     * 1. The total number of bytes we need to write the decomposition of the       \
     *    input bytes would be greater than 16.                                     \
     * 2. We've detected that combining characters are out-of-order (note that      \
     *    it is possible to use `ccc_lt` to sort characters in an "optimized"       \
     *    manner. But this led to a performance hit, likely because we would        \
     *    have to spill more onto the stack).                                       \
     *                                                                              \
     * Both of these conditions are rather uncommon, though.                        \
     */                                                                             \
    if (likely(total <= 16 && vmaxvq_u16(ccc_lt) == 0)) {                           \
      *last_ccc = vgetq_lane_u16(ccc_values, 5);                                    \
      neon_write_non_hangul_simple_utf8_##decomp_form(in, chars, delta,             \
                                                      values, *out);                \
      *out += total;                                                                \
    } else {                                                                        \
      neon_write_non_hangul_utf8_##decomp_form##_fallback(                          \
          values, chars, 6, out, out_length, input, last_ccc);                      \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* Generalized decomposition for a 16-byte input vector of UTF-8 code             \
   * points. The `chars` parameter is a 4x16-bit vector of BMP code points,         \
   * and the `n_bytes` parameter indicates how many bytes of the input vector       \
   * are used for the `chars` parameter. The `input` parameter is the original      \
   * pointer to UTF-8 bytes.                                                        \
   *                                                                                \
   * The code points here may or may not be Hangul. A faster variant of this        \
   * function is available if Hangul cannot be present.                             \
   * */                                                                             \
  __attribute__((always_inline)) static inline void                                 \
      neon_decompose_utf8_##decomp_form(uint8x16_t in, uint16x4_t chars,            \
                                        size_t n_bytes, const uint8_t *input,       \
                                        uint8_t **out, size_t out_length,           \
                                        uint8_t *last_ccc) {                        \
    uint16x4_t hangul_mask = neon_hangul_mask(chars);                               \
    bool hangul_result = vmaxv_u16(hangul_mask) > 0;                                \
    uint16x4_t index = vshr_n_u16(chars, 6);                                        \
    uint16x4_t block_index = {                                                      \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     0)],           \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     1)],           \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     2)],           \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     3)],           \
    };                                                                              \
    uint16x4_t masked = vand_u16(chars, vdup_n_u16(0x3F));                          \
    uint16x4_t data_offset = vadd_u16(block_index, masked);                         \
    uint16x4_t values = {                                                           \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 0)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 1)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 2)],                                                       \
        NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 3)],                                                       \
    };                                                                              \
    bool decomp_result = vmaxv_u16(values) > 3;                                     \
    /* Case where we have no Hangul syllables and no relevant characters */         \
    if (!hangul_result && !decomp_result) {                                         \
      *last_ccc = 0;                                                                \
      vst1q_u8(*out, in);                                                           \
      *out += n_bytes;                                                              \
      return;                                                                       \
    }                                                                               \
    int16x4_t delta = vshr_n_s16(vreinterpret_s16_u16(values), 11);                 \
    int16_t total = (int16_t)n_bytes + vaddv_s16(delta);                            \
    assert(total > 0);                                                              \
    uint16x4_t ccc_values = vand_u16(vshr_n_u16(values, 2), vdup_n_u16(0xFF));      \
    uint16x4_t shifted_ccc = vext_u16(vdup_n_u16(*last_ccc), ccc_values, 3);        \
    uint16x4_t starters = vceq_u16(ccc_values, vdup_n_u16(0));                      \
    /* We can use the special ccc value 255 for starters */                         \
    uint16x4_t ccc_fixup = vbsl_u16(starters, vdup_n_u16(255), ccc_values);         \
    uint16x4_t ccc_lt = vclt_u16(ccc_fixup, shifted_ccc);                           \
    if (!hangul_result && total <= 16 && vmaxv_u16(ccc_lt) == 0) {                  \
      *last_ccc = vget_lane_u16(ccc_values, 3);                                     \
      neon_write_non_hangul_simple_utf8_##decomp_form(                              \
          in, vcombine_u16(chars, vdup_n_u16(0)),                                   \
          vcombine_s16(delta, vdup_n_u16(0)),                                       \
          vcombine_u16(values, vdup_n_u16(0)), *out);                               \
      *out += total;                                                                \
    } else if (!hangul_result) {                                                    \
      neon_write_non_hangul_utf8_##decomp_form##_fallback(                          \
          vcombine_u16(values, vdup_n_u16(0)),                                      \
          vcombine_u16(chars, vdup_n_u16(0)), 4, out, out_length, input,            \
          last_ccc);                                                                \
    } else if (hangul_result && !decomp_result) {                                   \
      neon_decompose_hangul_utf8(chars, hangul_mask, out, input, last_ccc);         \
    } else {                                                                        \
      /* Case where we have both precomposed characters and Hangul syllables.       \
       * Very rare in practice, so we just fall back to the scalar                  \
       * implementation. */                                                         \
      *out += scalar_normalize_utf8_##decomp_form##_with_context(                   \
          input, n_bytes, *out, out_length, last_ccc);                              \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* NFD normalize up to 16 bytes of UTF-8 using an end of code point mask.         \
   * Returns the number of bytes consumed. */                                       \
  static size_t neon_normalize_masked_utf8_##decomp_form(                           \
      const uint8_t *input, uint64_t mask, uint8_t **out, size_t out_length,        \
      uint8_t *last_ccc) {                                                          \
    /* Count trailing ones to get number of ASCII bytes at the start of input       \
     */                                                                             \
    int t1 = __builtin_ctzll(~mask);                                                \
    /* Skip as many ASCII bytes as possible. We eagerly skip ASCII because,         \
     * even if the number of ASCII bytes is small, benchmarks show that the         \
     * cost of falling into the slow path for a majority ASCII input vector is      \
     * quite high, especially for heavily Roman alphabetic languages broken up      \
     * by occasional diacritics, such as Spanish or French. */                      \
    if (t1 > 2) {                                                                   \
      size_t min = t1 > 52 ? 52 : t1;                                               \
      neon_memcpy_small(*out, input);                                               \
      *out += min;                                                                  \
      *last_ccc = 0;                                                                \
      return min;                                                                   \
    }                                                                               \
                                                                                    \
    uint8x16_t in = vld1q_u8(input);                                                \
    uint16_t sml_mask = mask & 0xFFF;                                               \
                                                                                    \
    /* Fast path for 4 3-byte code points */                                        \
    if (sml_mask == 0x924) {                                                        \
      uint16x4_t chars = neon_parse_3_byte_utf8(in);                                \
      uint16_t min = vminv_u16(chars);                                              \
      uint16_t max = vmaxv_u16(chars);                                              \
                                                                                    \
      /* Precomposed Hangul range. Characters in this range are                     \
       * algorithmically decomposable with a few arithmetic operations. They        \
       * are the only precomposed characters we can decompose without a table       \
       * lookup.                                                                    \
       *                                                                            \
       * Algorithm described here:                                                  \
       * https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401 \
       */                                                                           \
      if (min >= NORMDATA_S_BASE &&                                                 \
          max < NORMDATA_S_BASE + NORMDATA_S_COUNT) {                               \
        neon_decompose_all_hangul_utf8(chars, out, last_ccc);                       \
        return 12;                                                                  \
      }                                                                             \
                                                                                    \
      /* Fallback path for 4 3-byte characters */                                   \
      neon_decompose_utf8_##decomp_form(in, chars, 12, input, out, out_length,      \
                                        last_ccc);                                  \
      return 12;                                                                    \
    }                                                                               \
                                                                                    \
    /* Six two-byte code points */                                                  \
    if (sml_mask == 0xAAA) {                                                        \
      uint16x8_t chars = neon_parse_2_byte_utf8_wide(in);                           \
      /* Precomposed Hangul syllables are not possible in 2 byte code points        \
       */                                                                           \
      neon_decompose_non_hangul_utf8_##decomp_form(in, chars, 12, input, out,       \
                                                   out_length, last_ccc);           \
      return 12;                                                                    \
    }                                                                               \
                                                                                    \
    uint8_t idx = NORMDATA_CODE_POINT_INDEX_WIDE[sml_mask][0];                      \
    uint8_t n_bytes = NORMDATA_CODE_POINT_INDEX_WIDE[sml_mask][1];                  \
                                                                                    \
    /* TODO: maybe worth putting a branch here for inputs like Don Quijote:         \
     *       mostly ASCII but with the occasional decomposition. In such            \
     *       cases, go scalar */                                                    \
    if (idx < NORMDATA_SHUFUTF8_WIDE_INDEX_12) {                                    \
      /* Six one to two byte code points */                                         \
      uint16x8_t chars = neon_parse_4_12_utf8_wide(in, idx);                        \
      /* Precomposed Hangul syllables are not possible in 1 to 2 byte code          \
       * points */                                                                  \
      neon_decompose_non_hangul_utf8_##decomp_form(in, chars, n_bytes, input,       \
                                                   out, out_length, last_ccc);      \
    } else if (idx < NORMDATA_SHUFUTF8_WIDE_INDEX_123) {                            \
      /* Four code points */                                                        \
      uint16x4_t chars = neon_parse_4_123_utf8_wide(in, idx);                       \
      neon_decompose_utf8_##decomp_form(in, chars, n_bytes, input, out,             \
                                        out_length, last_ccc);                      \
    } else if (idx < NORMDATA_SHUFUTF8_WIDE_INDEX_1234) {                           \
      /* TODO: right now, anytime we have 3 1..4-byte code points, we just          \
       *       fall back to scalar. This is because our functions are designed      \
       *       for 4 code points, and we don't have a good way to handle the        \
       *       case where we have 3 code points. Four byte code points are          \
       *       uncommon enough in regular text that I think this path is okay,      \
       *       though. */                                                           \
      *out += scalar_normalize_utf8_##decomp_form##_with_context(                   \
          input, n_bytes, *out, out_length, last_ccc);                              \
    }                                                                               \
                                                                                    \
    return n_bytes;                                                                 \
  }                                                                                 \
                                                                                    \
  /* Write four BMP code points that have value 0 or 1 from the NF(K)C trie.        \
   * This means that they do not compose with anything, so this function            \
   * essentially performs NF(K)D on the given code points.                          \
   */                                                                               \
  static void neon_write_no_comp_utf8_##comp_form(                                  \
      uint16x4_t values, uint16x4_t code_points, uint8_t **out,                     \
      size_t out_length, const uint8_t *input, uint8_t *last_ccc) {                 \
    uint8_t *start = *out;                                                          \
                                                                                    \
    for (size_t i = 0; i < 4; i++) {                                                \
      uint8_t leading = input[0];                                                   \
      if (leading <= 0x7F) {                                                        \
        *(*out)++ = leading;                                                        \
        input++;                                                                    \
        *last_ccc = 0;                                                              \
        continue;                                                                   \
      }                                                                             \
      uint16_t value = values[i];                                                   \
      uint8_t size = NORMDATA_UTF8_SIZE[leading];                                   \
      /* Check if we can skip */                                                    \
      if (value == 0) {                                                             \
        vst1_u8(*out, vld1_u8(input));                                              \
        *out += size;                                                               \
        input += size;                                                              \
        *last_ccc = 0;                                                              \
        continue;                                                                   \
      }                                                                             \
      /* Decompose the code point like we would in NF(K)D */                        \
      assert(value == 1);                                                           \
      uint16_t code_point = code_points[i];                                         \
      uint16_t shifted = code_point >> 6;                                           \
      uint16_t masked = code_point & 0x3F;                                          \
      uint16_t index =                                                              \
          NORMDATA_UTF8_##decomp_form_upper##_TRIE_INDEX[shifted];                  \
      uint32_t decomp_value =                                                       \
          NORMDATA_UTF8_##decomp_form_upper##_TRIE_DATA[index + masked];            \
      assert(decomp_value != 0);                                                    \
      const uint8_t *decomp_offset =                                                \
          &NORMDATA_UTF8_##decomp_form_upper##_TRIE_DECOMPOSITIONS                  \
              [decomp_value & 0xFFFF];                                              \
      uint8_t length = decomp_value >> 24;                                          \
      assert(length <= 8);                                                          \
      /* Note that decomposing this character might at first seem to break the      \
       * "important invariant" described in `neon_fallback_utf8_nf(k)c`, but        \
       * this is actually not the case. Any NF(K)C trie value 1 code points         \
       * are guaranteed to decompose into a single code point. So the byte          \
       * difference might not be the same, but the code point difference is.        \
       */                                                                           \
      vst1_u8(*out, vld1_u8(decomp_offset));                                        \
      *out += length;                                                               \
                                                                                    \
      uint8_t ccc = (decomp_value >> 16) & 0xFF;                                    \
      if (ccc != 0 && *last_ccc > ccc) {                                            \
        ccc = scalar_sort_characters_utf8(*out, out_length + (*out - start));       \
      }                                                                             \
      input += size;                                                                \
      *last_ccc = ccc;                                                              \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* Fallback to scalar implementation when encountering an NFC relevant            \
   * character. Returns the number of characters of the input consumed, and         \
   * updates the output pointer to be in the correct place. */                      \
  static size_t neon_fallback_utf8_##comp_form(                                     \
      const uint8_t *input, const uint8_t *input_base, size_t input_length,         \
      uint8_t **out, size_t length) {                                               \
    size_t offset = input - input_base;                                             \
    size_t first_size = NORMDATA_UTF8_SIZE[input[0]];                               \
    /* Get the region that we will NFC normalize */                                 \
    size_t prev_starter =                                                           \
        scalar_rfind_starter_utf8(input_base, offset + first_size);                 \
    if (prev_starter == (size_t)-1) {                                               \
      prev_starter = 0;                                                             \
    }                                                                               \
    size_t next_starter = scalar_find_first_stable_utf8_##comp_form(                \
        input_base + offset + length, input_length - offset - length);              \
    if (next_starter == (size_t)-1) {                                               \
      next_starter = input_length;                                                  \
    } else {                                                                        \
      next_starter += offset + length;                                              \
    }                                                                               \
    size_t region_size = next_starter - prev_starter;                               \
    size_t code_point_dist = scalar_count_code_points_utf8(                         \
        input_base + prev_starter, offset - prev_starter);                          \
    size_t prev_out_offset = scalar_get_code_point_pos_reverse_utf8(                \
        *out, SIZE_MAX, code_point_dist);                                           \
    /* This is the position we will write to. It is the same number of code         \
     * points away that the tail of the input is from the previous starter          \
     * code point. This property being true is an important invariant in the        \
     * algorithm, because we need to know where the left boundary of the            \
     * region we found is in the output buffer. */                                  \
    uint8_t *prev_out = *out - prev_out_offset;                                     \
    size_t nwritten = scalar_normalize_utf8_##comp_form(                            \
        input_base + prev_starter, region_size, prev_out);                          \
    *out = prev_out + nwritten;                                                     \
                                                                                    \
    return next_starter - offset;                                                   \
  }                                                                                 \
                                                                                    \
  static size_t neon_normalize_masked_utf8_##comp_form(                             \
      const uint8_t *input, const uint8_t *input_base, size_t input_length,         \
      uint64_t mask, uint8_t **out, size_t out_length, uint8_t *last_ccc) {         \
    int t1 = __builtin_ctzll(~mask);                                                \
    /* Eagerly skip ASCII, similar to NF(K)D */                                     \
    if (t1 > 2) {                                                                   \
      size_t min = t1 > 52 ? 52 : t1;                                               \
      neon_memcpy_small(*out, input);                                               \
      *out += min;                                                                  \
      *last_ccc = 0;                                                                \
      return min;                                                                   \
    }                                                                               \
                                                                                    \
    uint8x16_t in = vld1q_u8(input);                                                \
    uint16_t sml_mask = mask & 0xFFF;                                               \
                                                                                    \
    uint16x4_t code_points;                                                         \
    size_t n_bytes;                                                                 \
                                                                                    \
    if (sml_mask == 0x924) {                                                        \
      code_points = neon_parse_3_byte_utf8(in);                                     \
      n_bytes = 12;                                                                 \
    } else if ((sml_mask & 0xFF) == 0xAA) {                                         \
      code_points = neon_parse_2_byte_utf8(in);                                     \
      n_bytes = 8;                                                                  \
    } else {                                                                        \
      uint8_t idx = NORMDATA_CODE_POINT_INDEX[sml_mask][0];                         \
      n_bytes = NORMDATA_CODE_POINT_INDEX[sml_mask][1];                             \
      if (idx < NORMDATA_SHUFUTF8_INDEX_12) {                                       \
        code_points = neon_parse_4_12_utf8(in, idx);                                \
      } else if (idx < NORMDATA_SHUFUTF8_INDEX_123) {                               \
        code_points = neon_parse_4_123_utf8(in, idx);                               \
      } else {                                                                      \
        assert(idx < NORMDATA_SHUFUTF8_INDEX_1234);                                 \
        *last_ccc = 0;                                                              \
        return neon_fallback_utf8_##comp_form(input, input_base, input_length,      \
                                              out, n_bytes);                        \
      }                                                                             \
    }                                                                               \
                                                                                    \
    uint16x4_t values = neon_evaluate_trie_##comp_form(code_points);                \
    uint16_t max = vmaxv_u16(values);                                               \
    /* No relevant characters */                                                    \
    if (max == 0) {                                                                 \
      vst1q_u8(*out, in);                                                           \
      *out += n_bytes;                                                              \
      *last_ccc = 0;                                                                \
      return n_bytes;                                                               \
    }                                                                               \
    /* If the max value is 1, then we have only characters affected by NF(K)D,      \
     * not anything actually to compose (the first step of NF(K)C is to run         \
     * NF(K)D, and this garuantees that is the only thing we must do). This         \
     * allows us to cut out a large portion of work, especially for                 \
     * compatibility composition. */                                                \
    if (max == 1) {                                                                 \
      /* Special writing function that essentially runs NF(K)D on                   \
       * `code_points`. This is the only place we use last_ccc information. */      \
      neon_write_no_comp_utf8_##comp_form(values, code_points, out,                 \
                                          out_length, input, last_ccc);             \
      return n_bytes;                                                               \
    }                                                                               \
    *last_ccc = 0;                                                                  \
    return neon_fallback_utf8_##comp_form(input, input_base, input_length,          \
                                          out, n_bytes);                            \
  }                                                                                 \
                                                                                    \
  size_t neon_normalize_utf8_##decomp_form(const uint8_t *input,                    \
                                           size_t length, uint8_t *out) {           \
    uint8_t **out_ptr = &out;                                                       \
    uint8_t *start = out;                                                           \
                                                                                    \
    /* It is possible that we do buffer overruns (but only _use_ the                \
     * appropriate number of bytes) in specifc cases. This margin makes sure        \
     * that those oversized store operations are safe. */                           \
    const size_t SAFETY_MARGIN = 64;                                                \
    uint8_t last_ccc = 0;                                                           \
    size_t p = 0;                                                                   \
    while (p + 64 + SAFETY_MARGIN <= length) {                                      \
      uint64_t mask = neon_make_utf8_code_point_mask(input + p);                    \
      size_t pmax = (p + 64) - 12;                                                  \
      while (p < pmax) {                                                            \
        size_t consumed = neon_normalize_masked_utf8_##decomp_form(                 \
            input + p, mask, out_ptr, *out_ptr - start, &last_ccc);                 \
        p += consumed;                                                              \
        mask >>= consumed;                                                          \
      }                                                                             \
    }                                                                               \
                                                                                    \
    if (p < length) {                                                               \
      /* Write the rest using scalar code */                                        \
      *out_ptr += scalar_normalize_utf8_##decomp_form##_with_context(               \
          input + p, length - p, *out_ptr, *out_ptr - start, &last_ccc);            \
    }                                                                               \
                                                                                    \
    return *out_ptr - start;                                                        \
  }                                                                                 \
                                                                                    \
  size_t neon_normalize_utf8_##comp_form(const uint8_t *input, size_t length,       \
                                         uint8_t *out) {                            \
    uint8_t **out_ptr = &out;                                                       \
    uint8_t *start = out;                                                           \
                                                                                    \
    const size_t SAFETY_MARGIN = 64;                                                \
    uint8_t last_ccc = 0;                                                           \
    size_t p = 0;                                                                   \
    while (p + 64 + SAFETY_MARGIN <= length) {                                      \
      uint64_t mask = neon_make_utf8_code_point_mask(input + p);                    \
      size_t pmax = (p + 64) - 12;                                                  \
      while (p < pmax) {                                                            \
        size_t consumed = neon_normalize_masked_utf8_##comp_form(                   \
            input + p, input, length, mask, out_ptr, *out_ptr - start,              \
            &last_ccc);                                                             \
        p += consumed;                                                              \
        mask >>= consumed;                                                          \
      }                                                                             \
    }                                                                               \
                                                                                    \
    if (p < length) {                                                               \
      (void)neon_fallback_utf8_##comp_form(input + p, input, length, out_ptr,       \
                                           length - p);                             \
    }                                                                               \
                                                                                    \
    return *out_ptr - start;                                                        \
  }

NEON_DEFINE_NORMALIZE_FUNCTIONS(nfd, NFD, nfc, NFC, false);
NEON_DEFINE_NORMALIZE_FUNCTIONS(nfkd, NFKD, nfkc, NFKC, true);

#undef NEON_DEFINE_NORMALIZE_FUNCTIONS

#define NEON_DEFINE_NORMALIZE_LENGTH_FUNCTIONS(form, form_upper)               \
  static size_t neon_normalize_masked_utf8_##form##_length(                    \
      const uint8_t *input, uint64_t mask, size_t *out_length) {               \
    int t1 = __builtin_ctzll(~mask);                                           \
    if (t1 > 0) {                                                              \
      size_t min = t1 > 52 ? 52 : t1;                                          \
      *out_length += min;                                                      \
      return min;                                                              \
    }                                                                          \
    uint16_t sml_mask = mask & 0xFFF;                                          \
    uint16x4_t code_points;                                                    \
    size_t n_bytes;                                                            \
    uint8x16_t in = vld1q_u8(input);                                           \
    if (sml_mask == 0x924) {                                                   \
      code_points = neon_parse_3_byte_utf8(in);                                \
      n_bytes = 12;                                                            \
    } else if ((sml_mask & 0xFF) == 0xAA) {                                    \
      code_points = neon_parse_2_byte_utf8(in);                                \
      n_bytes = 8;                                                             \
    } else {                                                                   \
      uint8_t idx = NORMDATA_CODE_POINT_INDEX[sml_mask][0];                    \
      n_bytes = NORMDATA_CODE_POINT_INDEX[sml_mask][1];                        \
      if (idx < NORMDATA_SHUFUTF8_INDEX_12) {                                  \
        code_points = neon_parse_4_12_utf8(in, idx);                           \
      } else if (idx < NORMDATA_SHUFUTF8_INDEX_123) {                          \
        code_points = neon_parse_4_123_utf8(in, idx);                          \
      } else {                                                                 \
        assert(idx < NORMDATA_SHUFUTF8_INDEX_1234);                            \
        *out_length += scalar_normalize_utf8_##form##_length(input, n_bytes);  \
        return n_bytes;                                                        \
      }                                                                        \
    }                                                                          \
    uint16x4_t index = vshr_n_u16(code_points, 6);                             \
    uint16x4_t block_index = {                                                 \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_INDEX[vget_lane_u16(index,    \
                                                                     0)],      \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_INDEX[vget_lane_u16(index,    \
                                                                     1)],      \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_INDEX[vget_lane_u16(index,    \
                                                                     2)],      \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_INDEX[vget_lane_u16(index,    \
                                                                     3)],      \
    };                                                                         \
    uint16x4_t masked = vand_u16(code_points, vdup_n_u16(0x3F));               \
    uint16x4_t data_offset = vadd_u16(block_index, masked);                    \
    uint16x4_t values = {                                                      \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_DATA[vget_lane_u16(           \
            data_offset, 0)],                                                  \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_DATA[vget_lane_u16(           \
            data_offset, 1)],                                                  \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_DATA[vget_lane_u16(           \
            data_offset, 2)],                                                  \
        NORMDATA_UTF8_##form_upper##_LENGTH_TRIE_DATA[vget_lane_u16(           \
            data_offset, 3)],                                                  \
    };                                                                         \
    *out_length += vaddv_u16(values);                                          \
    return n_bytes;                                                            \
  }                                                                            \
                                                                               \
  size_t neon_normalize_utf8_##form##_length(const uint8_t *input,             \
                                             size_t length) {                  \
    size_t out_length = 0;                                                     \
    const size_t SAFETY_MARGIN = 64;                                           \
    size_t p = 0;                                                              \
    while (p + 64 + SAFETY_MARGIN <= length) {                                 \
      uint64_t mask = neon_make_utf8_code_point_mask(input + p);               \
      size_t pmax = (p + 64) - 12;                                             \
      while (p < pmax) {                                                       \
        size_t consumed = neon_normalize_masked_utf8_##form##_length(          \
            input + p, mask, &out_length);                                     \
        p += consumed;                                                         \
        mask >>= consumed;                                                     \
      }                                                                        \
    }                                                                          \
    if (p < length) {                                                          \
      out_length +=                                                            \
          scalar_normalize_utf8_##form##_length(input + p, length - p);        \
    }                                                                          \
    return out_length;                                                         \
  }

NEON_DEFINE_NORMALIZE_LENGTH_FUNCTIONS(nfd, NFD);
NEON_DEFINE_NORMALIZE_LENGTH_FUNCTIONS(nfkd, NFKD);
NEON_DEFINE_NORMALIZE_LENGTH_FUNCTIONS(nfc, NFC);
NEON_DEFINE_NORMALIZE_LENGTH_FUNCTIONS(nfkc, NFKC);

#undef NEON_DEFINE_NORMALIZE_LENGTH_FUNCTIONS

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
