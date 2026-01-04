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

#define unlikely(x) __builtin_expect(!!(x), 0)

#define NEON_DEFINE_NORMALIZE_FUNCTIONS(decomp_form, decomp_table_name,             \
                                        comp_form, comp_table_name,                 \
                                        large_decompositions)                       \
  /* Decompose code points into their UTF-8 representations. `values` is a          \
   * vector corresponding to 4 16-bit code points who have been looked up in        \
   * the NF(K)D Trie.                                                               \
   *                                                                                \
   * This function assumes that the input code points are not Hangul                \
   * syllables. */                                                                  \
  static void neon_write_non_hangul_utf8_##decomp_form(                             \
      uint32x4_t values, uint8_t **out, size_t out_length,                          \
      const uint8_t *input, uint8_t *last_ccc) {                                    \
    uint8_t *start = *out;                                                          \
                                                                                    \
    for (size_t i = 0; i < 4; i++) {                                                \
      uint8_t leading = input[0];                                                   \
      if (leading <= 0x7F) {                                                        \
        /* ASCII code point, no decomposition needed. */                            \
        *(*out)++ = leading;                                                        \
        input++;                                                                    \
        *last_ccc = 0;                                                              \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      uint32_t value = values[i];                                                   \
      uint8_t size = NORMDATA_UTF8_SIZE[leading];                                   \
      if (value == 0) {                                                             \
        vst1_u8(*out, vld1_u8(input));                                              \
        *out += size;                                                               \
        input += size;                                                              \
        *last_ccc = 0;                                                              \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      const uint8_t *decomp_offset =                                                \
          &NORMDATA_UTF8_##decomp_table_name##_TRIE_DECOMPOSITIONS[value &          \
                                                                   0xFFFF];         \
      uint8_t length = value >> 24;                                                 \
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
      /* `ccc` represents the combining class of the last character in the          \
       * decomposition of the character we're on, not the actual ccc value of       \
       * the character. */                                                          \
      uint8_t ccc = (value >> 16) & 0xFF;                                           \
      if (ccc != 0 && *last_ccc > ccc) {                                            \
        ccc = scalar_sort_characters_utf8(*out, out_length + (*out - start));       \
      }                                                                             \
      input += size;                                                                \
      *last_ccc = ccc;                                                              \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* Decompose code points into their UTF-8 representations. `values` is a          \
   * vector corresponding to 4 16-bit code points who have been looked up in        \
   * the NF(K)D Trie.                                                               \
   *                                                                                \
   * This function assumes that the input code points are not Hangul                \
   * syllables AND that they are all starter characters. */                         \
  static inline void neon_write_non_hangul_starters_utf8_##decomp_form(             \
      uint32x4_t values, uint8_t **out, const uint8_t *input,                       \
      uint8_t *last_ccc) {                                                          \
    *last_ccc = 0;                                                                  \
    for (size_t i = 0; i < 4; i++) {                                                \
      uint8_t leading = input[0];                                                   \
      if (leading <= 0x7F) {                                                        \
        /* ASCII code point, no decomposition needed. */                            \
        *(*out)++ = leading;                                                        \
        input++;                                                                    \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      uint32_t value = values[i];                                                   \
      uint8_t size = NORMDATA_UTF8_SIZE[leading];                                   \
      /* If we have zero, then it is not decomposable */                            \
      if (value == 0) {                                                             \
        vst1_u8(*out, vld1_u8(input));                                              \
        *out += size;                                                               \
        input += size;                                                              \
        continue;                                                                   \
      }                                                                             \
      const uint8_t *decomp_offset =                                                \
          &NORMDATA_UTF8_##decomp_table_name##_TRIE_DECOMPOSITIONS[value &          \
                                                                   0xFFFF];         \
      /* The length value here corresponds to how many code points we should        \
       * copy from NORMDATA_UTF8_NF(K)D_TRIE_DECOMPOSITIONS. */                     \
      uint8_t length = value >> 24;                                                 \
      vst1q_u8(*out, vld1q_u8(decomp_offset));                                      \
      if (large_decompositions && unlikely(length > 16)) {                          \
        vst1q_u8(*out + 16, vld1q_u8(decomp_offset + 16));                          \
        for (size_t j = 32; j < length; j++) {                                      \
          (*out)[j] = decomp_offset[j];                                             \
        }                                                                           \
      }                                                                             \
      *out += length;                                                               \
      input += size;                                                                \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* Decompose input code points, assuming they are not precomposed Hangul          \
   * syllables. `in` is a vector of the raw input bytes, and `n_bytes` is the       \
   * number of bytes that the four `chars` code points occupy within the 16         \
   * byte `in` vector. */                                                           \
  __attribute__((always_inline)) static inline void                                 \
      neon_decompose_non_hangul_utf8_##decomp_form(                                 \
          uint8x16_t in, uint16x4_t chars, size_t n_bytes,                          \
          const uint8_t *input, uint8_t **out, size_t out_length,                   \
          uint8_t *last_ccc) {                                                      \
    uint16x4_t index = vshr_n_u16(chars, 6);                                        \
    uint16x4_t block_index = {                                                      \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     0)],           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     1)],           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     2)],           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     3)],           \
    };                                                                              \
    uint16x4_t masked = vand_u16(chars, vdup_n_u16(0x3F));                          \
    uint16x4_t data_offset = vadd_u16(block_index, masked);                         \
    uint32x4_t values = {                                                           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 0)],                                                       \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 1)],                                                       \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 2)],                                                       \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 3)],                                                       \
    };                                                                              \
    /* In this case, all of our code points have value 0, which means we can        \
     * skip. */                                                                     \
    if (vmaxvq_u32(values) == 0) {                                                  \
      *last_ccc = 0;                                                                \
      vst1q_u8(*out, in);                                                           \
      *out += n_bytes;                                                              \
      return;                                                                       \
    }                                                                               \
    /* Extract the ccc values from each code point */                               \
    uint32x4_t ccc_mask = vandq_u32(values, vdupq_n_u32(0x00FF0000UL));             \
    /* Check if we have no combining characters, in which case we can do a          \
     * specialized version of the write function. */                                \
    if (vmaxvq_u32(ccc_mask) == 0) {                                                \
      neon_write_non_hangul_starters_utf8_##decomp_form(values, out, input,         \
                                                        last_ccc);                  \
    } else {                                                                        \
      /* Slow path */                                                               \
      neon_write_non_hangul_utf8_##decomp_form(values, out, out_length, input,      \
                                               last_ccc);                           \
    }                                                                               \
  }                                                                                 \
                                                                                    \
  /* Generalized decomposition for a 16-byte input vector of UTF-8 code             \
   * points. The parsed code points are passed in as a 4x32-bit vector of code      \
   * points, and the `n_bytes` parameter indicates how many bytes of the input      \
   * vector are used for the `chars` parameter. The `input` parameter is the        \
   * original pointer to UTF-8 bytes. */                                            \
  static inline void neon_decompose_utf8_##decomp_form(                             \
      uint8x16_t in, uint16x4_t chars, size_t n_bytes, const uint8_t *input,        \
      uint8_t **out, size_t out_length, uint8_t *last_ccc) {                        \
    uint16x4_t hangul_mask = neon_hangul_mask(chars);                               \
    bool hangul_result = vmaxv_u16(hangul_mask) > 0;                                \
    uint16x4_t index = vshr_n_u16(chars, 6);                                        \
    uint16x4_t block_index = {                                                      \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     0)],           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     1)],           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     2)],           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_INDEX[vget_lane_u16(index,         \
                                                                     3)],           \
    };                                                                              \
    uint16x4_t masked = vand_u16(chars, vdup_n_u16(0x3F));                          \
    uint16x4_t data_offset = vadd_u16(block_index, masked);                         \
    uint32x4_t values = {                                                           \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 0)],                                                       \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 1)],                                                       \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 2)],                                                       \
        NORMDATA_UTF8_##decomp_table_name##_TRIE_DATA[vget_lane_u16(                \
            data_offset, 3)],                                                       \
    };                                                                              \
    bool decomp_result = vmaxvq_u32(values) > 0;                                    \
    /* Case where we have no Hangul syllables and no relevant characters */         \
    if (!hangul_result && !decomp_result) {                                         \
      *last_ccc = 0;                                                                \
      vst1q_u8(*out, in);                                                           \
      *out += n_bytes;                                                              \
      return;                                                                       \
    }                                                                               \
    uint32x4_t ccc_mask = vandq_u32(values, vdupq_n_u32(0x00FF0000UL));             \
    bool non_starter_result = vmaxvq_u32(ccc_mask) > 0;                             \
    if (!non_starter_result && !hangul_result) {                                    \
      neon_write_non_hangul_starters_utf8_##decomp_form(values, out, input,         \
                                                        last_ccc);                  \
    } else if (non_starter_result && !hangul_result) {                              \
      neon_write_non_hangul_utf8_##decomp_form(values, out, out_length, input,      \
                                               last_ccc);                           \
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
      /* The large, common 3-byte CJK range (encompasses much of CJK) that has      \
       * no precomposed code points. We can just skip these. Not a huge fan of      \
       * language-specific optimization like this (excluding ASCII), but the        \
       * speedups are so large for such a low cost that it seems worth it. */       \
      if (min >= 0x30FF && max <= 0x9FFF) {                                         \
        *last_ccc = 0;                                                              \
        vst1q_u8(*out, in);                                                         \
        *out += 12;                                                                 \
        return 12;                                                                  \
      }                                                                             \
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
    /* Four two-byte code points */                                                 \
    if ((sml_mask & 0xFF) == 0xAA) {                                                \
      uint16x4_t chars = neon_parse_2_byte_utf8(in);                                \
      /* Precomposed Hangul syllables are not possible in 2 byte code points        \
       */                                                                           \
      neon_decompose_non_hangul_utf8_##decomp_form(in, chars, 8, input, out,        \
                                                   out_length, last_ccc);           \
      return 8;                                                                     \
    }                                                                               \
                                                                                    \
    uint8_t idx = NORMDATA_CODE_POINT_INDEX[sml_mask][0];                           \
    uint8_t n_bytes = NORMDATA_CODE_POINT_INDEX[sml_mask][1];                       \
                                                                                    \
    if (idx < NORMDATA_SHUFUTF8_INDEX_12) {                                         \
      /* Four one to two byte code points */                                        \
      uint16x4_t chars = neon_parse_4_12_utf8(in, idx);                             \
      /* Precomposed Hangul syllables are not possible in 1 to 2 byte code          \
       * points */                                                                  \
      neon_decompose_non_hangul_utf8_##decomp_form(in, chars, n_bytes, input,       \
                                                   out, out_length, last_ccc);      \
    } else if (idx < NORMDATA_SHUFUTF8_INDEX_123) {                                 \
      /* Four code points */                                                        \
      uint16x4_t chars = neon_parse_4_123_utf8(in, idx);                            \
      neon_decompose_utf8_##decomp_form(in, chars, n_bytes, input, out,             \
                                        out_length, last_ccc);                      \
    } else if (idx < NORMDATA_SHUFUTF8_INDEX_1234) {                                \
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
    size_t next_starter = scalar_find_##comp_form##_irrelevant_starter_utf8(        \
        input_base + offset + length, input_length - offset - length);              \
    if (next_starter == (size_t)-1) {                                               \
      next_starter = input_length;                                                  \
    } else {                                                                        \
      next_starter += offset + length;                                              \
    }                                                                               \
    size_t region_size = next_starter - prev_starter;                               \
    /* This is the position we will write to */                                     \
    uint8_t *prev_out = *out - (offset - prev_starter);                             \
    size_t nwritten = scalar_normalize_utf8_##comp_form(                            \
        input_base + prev_starter, region_size, prev_out);                          \
    *out = prev_out + nwritten;                                                     \
                                                                                    \
    return next_starter - offset;                                                   \
  }                                                                                 \
                                                                                    \
  static size_t neon_normalize_masked_utf8_##comp_form(                             \
      const uint8_t *input, const uint8_t *input_base, size_t length,               \
      uint64_t mask, uint8_t **out) {                                               \
    int t1 = __builtin_ctzll(~mask);                                                \
    /* Eagerly skip ASCII, similar to NF(K)D */                                     \
    if (t1 > 2) {                                                                   \
      size_t min = t1 > 52 ? 52 : t1;                                               \
      neon_memcpy_small(*out, input);                                               \
      *out += min;                                                                  \
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
        return neon_fallback_utf8_##comp_form(input, input_base, length, out,       \
                                              n_bytes);                             \
      }                                                                             \
    }                                                                               \
                                                                                    \
    uint16x4_t values = neon_evaluate_trie_##comp_form(code_points);                \
    if (vmaxv_u16(values) == 0) {                                                   \
      vst1q_u8(*out, in);                                                           \
      *out += n_bytes;                                                              \
      return n_bytes;                                                               \
    }                                                                               \
    return neon_fallback_utf8_##comp_form(input, input_base, length, out,           \
                                          n_bytes);                                 \
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
    size_t p = 0;                                                                   \
    while (p + 64 + SAFETY_MARGIN <= length) {                                      \
      uint64_t mask = neon_make_utf8_code_point_mask(input + p);                    \
      size_t pmax = (p + 64) - 12;                                                  \
      while (p < pmax) {                                                            \
        size_t consumed = neon_normalize_masked_utf8_##comp_form(                   \
            input + p, input, length, mask, out_ptr);                               \
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

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
