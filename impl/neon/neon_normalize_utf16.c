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

#define NEON_UTF16_HELPERS(endianness, swap_endianness)                        \
  /* Copy the input vector into the output buffer. */                          \
  static void neon_skip_decomp_utf16##endianness(                              \
      uint16x8_t in, size_t length, uint8_t **out, uint8_t *last_ccc) {        \
    *last_ccc = 0;                                                             \
    if (swap_endianness) {                                                     \
      in = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(in)));         \
    }                                                                          \
    uint8x16_t bytes = vreinterpretq_u8_u16(in);                               \
    vst1q_u8(*out, bytes);                                                     \
    *out += length;                                                            \
  }

NEON_UTF16_HELPERS(le, XXUTF_BIG_ENDIAN);
NEON_UTF16_HELPERS(be, !XXUTF_BIG_ENDIAN);

#undef NEON_UTF16_HELPERS

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define NEON_UTF16_IMPLEMENTATION(endianness, swap_endianness, is_big_endian,  \
                                  decomp_form, decomp_form_upper, comp_form,   \
                                  comp_form_upper, large_decompositions)       \
  /* Decompose UTF-16 code points that have some number of precomposed Hangul  \
   * syllables in them, but no table-based decompositions. */                  \
  static inline void neon_write_hangul_utf16##endianness##_##decomp_form(      \
      uint16x4_t in, uint16x4_t relevant, uint8_t **out, const uint8_t *input, \
      uint8_t *last_ccc) {                                                     \
    *last_ccc = 0;                                                             \
                                                                               \
    /* Naively compute Hangul Jamo. In practice, if this function is called,   \
     * most characters will be precomposed syllables (for example, when an     \
     * ASCII space is present in between some Korean text). */                 \
    uint16x4x3_t lvt = neon_compute_hangul_jamo(in);                           \
    for (size_t i = 0; i < 4; i++) {                                           \
      /* Copy from the input if it is not a precomposed Hangul syllable. */    \
      if (relevant[i] == 0) {                                                  \
        *(*out)++ = input[0];                                                  \
        *(*out)++ = input[1];                                                  \
        input += 2;                                                            \
        continue;                                                              \
      }                                                                        \
                                                                               \
      uint16_t l = lvt.val[0][i];                                              \
      uint16_t v = lvt.val[1][i];                                              \
      uint16_t t = lvt.val[2][i];                                              \
                                                                               \
      scalar_write_uint16##endianness(l, *out);                                \
      *out += 2;                                                               \
      scalar_write_uint16##endianness(v, *out);                                \
      *out += 2;                                                               \
      scalar_write_uint16##endianness(t, *out);                                \
      *out += 2 * (t - NORMDATA_T_BASE > 0);                                   \
      input += 2;                                                              \
    }                                                                          \
  }                                                                            \
                                                                               \
  /* Decompose UTF-16 code points that have some number of precomposed or      \
   * combining characters in them, but no precomposed Hangul syllables. */     \
  static inline void neon_write_non_hangul_utf16##endianness##_##decomp_form(  \
      uint32x4_t values, uint8_t **out, size_t out_length,                     \
      const uint8_t *input, uint8_t *last_ccc) {                               \
    for (size_t i = 0; i < 4; i++) {                                           \
      uint32_t value = values[i];                                              \
      if (value == 0) {                                                        \
        *(*out)++ = *input++;                                                  \
        *(*out)++ = *input++;                                                  \
        *last_ccc = 0;                                                         \
        continue;                                                              \
      }                                                                        \
      uint8_t ccc = (value >> 16) & 0xFF;                                      \
      const uint8_t *decomp_offset =                                           \
          &NORMDATA_UTF16_##decomp_form_upper##_TRIE_DECOMPOSITIONS[value &    \
                                                                    0xFFFF];   \
      uint8_t length = value >> 24;                                            \
      /* `length` is length in bytes, not code units */                        \
      assert(length % 2 == 0);                                                 \
      uint8x16_t decomp_bytes = vld1q_u8(decomp_offset);                       \
      if (is_big_endian) {                                                     \
        decomp_bytes = vrev16q_u8(decomp_bytes);                               \
      }                                                                        \
      vst1q_u8(*out, decomp_bytes);                                            \
      if (large_decompositions && unlikely(length > 16)) {                     \
        for (size_t j = 16; j < length; j += 2) {                              \
          if (is_big_endian) {                                                 \
            (*out)[j] = decomp_offset[j + 1];                                  \
            (*out)[j + 1] = decomp_offset[j];                                  \
          } else {                                                             \
            (*out)[j] = decomp_offset[j];                                      \
            (*out)[j + 1] = decomp_offset[j + 1];                              \
          }                                                                    \
        }                                                                      \
      }                                                                        \
      *out += length;                                                          \
      out_length += length;                                                    \
      if (ccc != 0 && *last_ccc > ccc) {                                       \
        ccc = scalar_sort_characters_utf16##endianness(*out, out_length);      \
      }                                                                        \
      input += 2;                                                              \
      *last_ccc = ccc;                                                         \
    }                                                                          \
  }                                                                            \
                                                                               \
  static inline void                                                           \
      neon_write_non_hangul_starters_utf16##endianness##_##decomp_form(        \
          uint32x4_t values, uint8_t **out, const uint8_t *input,              \
          uint8_t *last_ccc) {                                                 \
    *last_ccc = 0;                                                             \
    for (size_t i = 0; i < 4; i++) {                                           \
      uint32_t value = values[i];                                              \
      if (value == 0) {                                                        \
        *(*out)++ = *input++;                                                  \
        *(*out)++ = *input++;                                                  \
        continue;                                                              \
      }                                                                        \
      const uint8_t *decomp_offset =                                           \
          &NORMDATA_UTF16_##decomp_form_upper##_TRIE_DECOMPOSITIONS[value &    \
                                                                    0xFFFF];   \
      uint8_t length = value >> 24;                                            \
      assert(length % 2 == 0);                                                 \
      uint8x16_t decomp_bytes = vld1q_u8(decomp_offset);                       \
      if (is_big_endian) {                                                     \
        decomp_bytes = vrev16q_u8(decomp_bytes);                               \
      }                                                                        \
      vst1q_u8(*out, decomp_bytes);                                            \
      if (large_decompositions && unlikely(length > 16)) {                     \
        for (size_t j = 16; j < length; j += 2) {                              \
          if (is_big_endian) {                                                 \
            (*out)[j] = decomp_offset[j + 1];                                  \
            (*out)[j + 1] = decomp_offset[j];                                  \
          } else {                                                             \
            (*out)[j] = decomp_offset[j];                                      \
            (*out)[j + 1] = decomp_offset[j + 1];                              \
          }                                                                    \
        }                                                                      \
      }                                                                        \
      *out += length;                                                          \
      input += 2;                                                              \
    }                                                                          \
  }                                                                            \
                                                                               \
  /* Decompose four UTF-16 code points in the BMP. */                          \
  static void neon_decompose_utf16##endianness##_##decomp_form(                \
      uint16x4_t chars, const uint8_t *input, uint8_t **out,                   \
      size_t out_length, uint8_t *last_ccc) {                                  \
    uint16x4_t hangul_mask = neon_hangul_mask(chars);                          \
    bool hangul_result = vmaxv_u16(hangul_mask) > 0;                           \
    uint16x4_t index = vshr_n_u16(chars, 6);                                   \
    uint16x4_t block_index = {                                                 \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,   \
                                                                      0)],     \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,   \
                                                                      1)],     \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,   \
                                                                      2)],     \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[vget_lane_u16(index,   \
                                                                      3)],     \
    };                                                                         \
    uint16x4_t masked = vand_u16(chars, vdup_n_u16(0x3F));                     \
    uint16x4_t data_offset = vadd_u16(block_index, masked);                    \
    uint32x4_t values = {                                                      \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(          \
            data_offset, 0)],                                                  \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(          \
            data_offset, 1)],                                                  \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(          \
            data_offset, 2)],                                                  \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[vget_lane_u16(          \
            data_offset, 3)],                                                  \
    };                                                                         \
    bool decomp_result = vmaxvq_u32(values) > 0;                               \
    /* With no Hangul characters and no decomposable/combining code points, we \
     * can skip */                                                             \
    if (!hangul_result && !decomp_result) {                                    \
      uint16x8_t in_dummy = vcombine_u16(chars, vdup_n_u16(0));                \
      neon_skip_decomp_utf16##endianness(in_dummy, 8, out, last_ccc);          \
      return;                                                                  \
    }                                                                          \
    uint32x4_t ccc_mask = vandq_u32(values, vdupq_n_u32(0x00FF0000UL));        \
    bool non_starter_result = vmaxvq_u32(ccc_mask) > 0;                        \
    if (!non_starter_result && !hangul_result) {                               \
      /* Special path with no combining code points and no Hangul characters   \
       */                                                                      \
      neon_write_non_hangul_starters_utf16##endianness##_##decomp_form(        \
          values, out, input, last_ccc);                                       \
    } else if (non_starter_result && !hangul_result) {                         \
      /* Path with combining code points but no Hangul characters */           \
      neon_write_non_hangul_utf16##endianness##_##decomp_form(                 \
          values, out, out_length, input, last_ccc);                           \
    } else if (hangul_result && !decomp_result) {                              \
      /* Path with only Hangul characters but no decomposable/combining code   \
       * points */                                                             \
      neon_write_hangul_utf16##endianness##_##decomp_form(                     \
          chars, hangul_mask, out, input, last_ccc);                           \
    } else {                                                                   \
      *out +=                                                                  \
          scalar_normalize_utf16##endianness##_##decomp_form##_with_context(   \
              input, 8, *out, out_length, last_ccc);                           \
    }                                                                          \
  }                                                                            \
                                                                               \
  size_t neon_normalize_utf16##endianness##_##decomp_form(                     \
      const uint8_t *input, size_t length, uint8_t *out) {                     \
    uint8_t **out_ptr = &out;                                                  \
    uint8_t *start = out;                                                      \
                                                                               \
    const size_t SAFETY_MARGIN = 16;                                           \
    uint8_t last_ccc = 0;                                                      \
    size_t p = 0;                                                              \
    while (p + SAFETY_MARGIN < length) {                                       \
      uint8x16_t bytes = vld1q_u8(input + p);                                  \
      uint16x8_t in = vreinterpretq_u16_u8(bytes);                             \
      if (swap_endianness) {                                                   \
        in = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(in)));       \
      }                                                                        \
      uint16x8_t ascii_mask = vcleq_u16(in, vdupq_n_u16(0x7F));                \
      /* ASCII fast path */                                                    \
      if (vminvq_u16(ascii_mask) != 0) {                                       \
        neon_skip_decomp_utf16##endianness(in, 16, out_ptr, &last_ccc);        \
        p += 16;                                                               \
        continue;                                                              \
      }                                                                        \
      uint16x8_t surrogates_mask = neon_make_utf16_surrogates_mask(in);        \
      /* Check if we have no surrogate pairs */                                \
      if (vaddvq_u32(surrogates_mask) == 0) {                                  \
        uint16x4_t in1 = vget_low_u16(in);                                     \
        uint16x4_t in2 = vget_high_u16(in);                                    \
        /* Decompose the low code points and the high code points separately   \
         */                                                                    \
        neon_decompose_utf16##endianness##_##decomp_form(                      \
            in1, input + p, out_ptr, *out_ptr - start, &last_ccc);             \
        neon_decompose_utf16##endianness##_##decomp_form(                      \
            in2, input + p + 8, out_ptr, *out_ptr - start, &last_ccc);         \
      } else {                                                                 \
        /* In the case that we do have surrogate pairs, we fall back to a      \
         * scalar implementation */                                            \
        size_t normalize_range = 16;                                           \
        if (vgetq_lane_u16(surrogates_mask, 7) == 0xFFFF) {                    \
          /* Include the low surrogate in the normalization range */           \
          normalize_range += 2;                                                \
        }                                                                      \
        *out_ptr +=                                                            \
            scalar_normalize_utf16##endianness##_##decomp_form##_with_context( \
                input + p, normalize_range, *out_ptr, *out_ptr - start,        \
                &last_ccc);                                                    \
        /* Advance the input possibly by 2 more */                             \
        p += normalize_range - 16;                                             \
      }                                                                        \
      p += 16;                                                                 \
    }                                                                          \
                                                                               \
    if (p < length) {                                                          \
      *out_ptr +=                                                              \
          scalar_normalize_utf16##endianness##_##decomp_form##_with_context(   \
              input + p, length - p, *out_ptr, *out_ptr - start, &last_ccc);   \
    }                                                                          \
                                                                               \
    return *out_ptr - start;                                                   \
  }                                                                            \
                                                                               \
  static void neon_write_no_comp_utf16##endianness##_##comp_form(              \
      uint16x8_t values, uint16x8_t code_points, uint8_t **out,                \
      size_t out_length, const uint8_t *input, uint8_t *last_ccc) {            \
    uint8_t *start = *out;                                                     \
                                                                               \
    for (size_t i = 0; i < 8; i++) {                                           \
      uint16_t value = values[i];                                              \
      if (value == 0) {                                                        \
        *(*out)++ = *input++;                                                  \
        *(*out)++ = *input++;                                                  \
        *last_ccc = 0;                                                         \
        continue;                                                              \
      }                                                                        \
      assert(value == 1);                                                      \
      uint16_t code_point = code_points[i];                                    \
      uint16_t shifted = code_point >> 6;                                      \
      uint16_t masked = code_point & 0x3F;                                     \
      uint16_t index =                                                         \
          NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[shifted];            \
      uint32_t decomp_value =                                                  \
          NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[index + masked];      \
      assert(decomp_value != 0);                                               \
      const uint8_t *decomp_offset =                                           \
          &NORMDATA_UTF16_##decomp_form_upper##_TRIE_DECOMPOSITIONS            \
              [decomp_value & 0xFFFF];                                         \
      uint8_t length = decomp_value >> 24;                                     \
      assert(length <= 8);                                                     \
      uint8x8_t decomp_bytes = vld1_u8(decomp_offset);                         \
      if (is_big_endian) {                                                     \
        decomp_bytes = vrev16_u8(decomp_bytes);                                \
      }                                                                        \
      vst1_u8(*out, decomp_bytes);                                             \
      *out += length;                                                          \
                                                                               \
      uint8_t ccc = (decomp_value >> 16) & 0xFF;                               \
      if (ccc != 0 && *last_ccc > ccc) {                                       \
        ccc = scalar_sort_characters_utf16##endianness(                        \
            *out, out_length + (*out - start));                                \
      }                                                                        \
      input += 2;                                                              \
      *last_ccc = ccc;                                                         \
    }                                                                          \
  }                                                                            \
                                                                               \
  /* TODO: this looks a lot like the UTF-8 version... use macro? */            \
  static size_t neon_fallback_utf16##endianness##_##comp_form(                 \
      const uint8_t *input, const uint8_t *input_base, size_t input_length,    \
      uint8_t **out, size_t length) {                                          \
    size_t offset = input - input_base;                                        \
    /* Get the region that we will NFC normalize */                            \
    /* TODO: should this be +2 or +4 in case the first code point is a high    \
     * surrogate? Write a specific test case for this */                       \
    size_t prev_starter =                                                      \
        scalar_rfind_starter_utf16##endianness(input_base, offset + 2);        \
    if (prev_starter == (size_t)-1) {                                          \
      prev_starter = 0;                                                        \
    }                                                                          \
    size_t next_starter =                                                      \
        scalar_find_##comp_form##_irrelevant_starter_utf16##endianness(        \
            input_base + offset + length, input_length - offset - length);     \
    if (next_starter == (size_t)-1) {                                          \
      next_starter = input_length;                                             \
    } else {                                                                   \
      next_starter += offset + length;                                         \
    }                                                                          \
    size_t region_size = next_starter - prev_starter;                          \
    size_t code_point_dist = scalar_count_code_points_utf16##endianness(       \
        input_base + prev_starter, offset - prev_starter);                     \
    size_t prev_out_offset =                                                   \
        scalar_get_code_point_pos_reverse_utf16##endianness(*out, SIZE_MAX,    \
                                                            code_point_dist);  \
    uint8_t *prev_out = *out - prev_out_offset;                                \
    size_t nwritten = scalar_normalize_utf16##endianness##_##comp_form(        \
        input_base + prev_starter, region_size, prev_out);                     \
    *out = prev_out + nwritten;                                                \
                                                                               \
    return next_starter - offset;                                              \
  }                                                                            \
                                                                               \
  size_t neon_normalize_utf16##endianness##_##comp_form(                       \
      const uint8_t *input, size_t length, uint8_t *out) {                     \
    uint8_t **out_ptr = &out;                                                  \
    uint8_t *start = out;                                                      \
                                                                               \
    const size_t SAFETY_MARGIN = 16;                                           \
    uint8_t last_ccc = 0;                                                      \
    size_t p = 0;                                                              \
    while (p + SAFETY_MARGIN < length) {                                       \
      uint8x16_t bytes = vld1q_u8(input + p);                                  \
      uint16x8_t in = vreinterpretq_u16_u8(bytes);                             \
      if (swap_endianness) {                                                   \
        in = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(in)));       \
      }                                                                        \
      uint16x8_t ascii_mask = vcleq_u16(in, vdupq_n_u16(0x7F));                \
      /* ASCII fast path */                                                    \
      if (vminvq_u16(ascii_mask) != 0) {                                       \
        /* Copy the original bytes. No need to swap the endianness here */     \
        vst1q_u8(*out_ptr, bytes);                                             \
        *out_ptr += 16;                                                        \
        p += 16;                                                               \
        last_ccc = 0;                                                          \
        continue;                                                              \
      }                                                                        \
      uint16x8_t surrogates_mask = neon_make_utf16_surrogates_mask(in);        \
      /* Check if we have no surrogate pairs */                                \
      if (vmaxvq_u32(surrogates_mask) == 0) {                                  \
        uint16x8_t trie = neon_evaluate_trie_compound_##comp_form(in);         \
        uint16_t max = vmaxvq_u16(trie);                                       \
        /* Skip if we have no relevant code points */                          \
        if (max == 0) {                                                        \
          vst1q_u8(*out_ptr, bytes);                                           \
          *out_ptr += 16;                                                      \
          p += 16;                                                             \
          last_ccc = 0;                                                        \
          continue;                                                            \
        }                                                                      \
        if (max == 1) {                                                        \
          neon_write_no_comp_utf16##endianness##_##comp_form(                  \
              trie, in, out_ptr, *out_ptr - start, input + p, &last_ccc);      \
          p += 16;                                                             \
          continue;                                                            \
        }                                                                      \
        /* Try just the lower part of the 8 code points */                     \
        uint16x4_t trie1 = vget_low_u16(trie);                                 \
        if (vmaxv_u16(trie1) == 0) {                                           \
          uint8x8_t low_bytes = vget_low_u8(bytes);                            \
          vst1_u8(*out_ptr, low_bytes);                                        \
          *out_ptr += 8;                                                       \
          p += 8;                                                              \
          last_ccc = 0;                                                        \
        } else {                                                               \
          /* Fall back to scalar */                                            \
          p += neon_fallback_utf16##endianness##_##comp_form(                  \
              input + p, input, length, out_ptr, 8);                           \
          last_ccc = 0;                                                        \
        }                                                                      \
      } else {                                                                 \
        /* With surrogate pairs, we fall back to the scalar implementation */  \
        size_t normalize_range = 16;                                           \
        /* We might have a trailing high surrogate, in which case we should    \
         * include the corresponding low surrogate in the normalization region \
         */                                                                    \
        if (vgetq_lane_u16(surrogates_mask, 7) == 0xFFFF) {                    \
          normalize_range += 2;                                                \
        }                                                                      \
        size_t normalized = neon_fallback_utf16##endianness##_##comp_form(     \
            input + p, input, length, out_ptr, normalize_range);               \
        p += normalized;                                                       \
        last_ccc = 0;                                                          \
      }                                                                        \
    }                                                                          \
                                                                               \
    if (p < length) {                                                          \
      (void)neon_fallback_utf16##endianness##_##comp_form(                     \
          input + p, input, length, out_ptr, length - p);                      \
    }                                                                          \
                                                                               \
    return *out_ptr - start;                                                   \
  }                                                                            \
                                                                               \
  size_t neon_normalize_utf16##endianness##_##decomp_form##_length(            \
      const uint8_t *input, size_t length) {                                   \
    size_t out_length = 0;                                                     \
    const size_t SAFETY_MARGIN = 16;                                           \
    size_t p = 0;                                                              \
    while (p + SAFETY_MARGIN < length) {                                       \
      uint8x16_t bytes = vld1q_u8(input + p);                                  \
      uint16x8_t in = vreinterpretq_u16_u8(bytes);                             \
      if (swap_endianness) {                                                   \
        in = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(in)));       \
      }                                                                        \
      uint16x8_t ascii_mask = vcleq_u16(in, vdupq_n_u16(0x7F));                \
      if (vminvq_u16(ascii_mask) != 0) {                                       \
        out_length += 16;                                                      \
        p += 16;                                                               \
        continue;                                                              \
      }                                                                        \
      uint16x8_t surrogates_mask = neon_make_utf16_surrogates_mask(in);        \
      if (vmaxvq_u16(surrogates_mask) == 0) {                                  \
        uint16x8_t index = vshrq_n_u16(in, 6);                                 \
        uint16x8_t block_index = {                                             \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 0)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 1)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 2)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 3)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 4)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 5)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 6)],                                    \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX             \
                [vgetq_lane_u16(index, 7)],                                    \
        };                                                                     \
        uint16x8_t masked = vandq_u16(in, vdupq_n_u16(0x3F));                  \
        uint16x8_t data_offset = vaddq_u16(block_index, masked);               \
        uint16x8_t values = {                                                  \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 0)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 1)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 2)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 3)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 4)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 5)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 6)],                              \
            NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA              \
                [vgetq_lane_u16(data_offset, 7)],                              \
        };                                                                     \
        out_length += vaddvq_u16(values);                                      \
      } else {                                                                 \
        size_t normalize_range = 16;                                           \
        if (vgetq_lane_u16(surrogates_mask, 7) == 0xFFFF) {                    \
          normalize_range += 2;                                                \
        }                                                                      \
        out_length +=                                                          \
            scalar_normalize_utf16##endianness##_##decomp_form##_length(       \
                input + p, normalize_range);                                   \
        p += normalize_range - 16;                                             \
      }                                                                        \
      p += 16;                                                                 \
    }                                                                          \
    if (p < length) {                                                          \
      out_length +=                                                            \
          scalar_normalize_utf16##endianness##_##decomp_form##_length(         \
              input + p, length - p);                                          \
    }                                                                          \
    return out_length;                                                         \
  }

NEON_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, false, nfd, NFD, nfc, NFC,
                          false);
NEON_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, false, nfkd, NFKD, nfkc, NFKC,
                          true);
NEON_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, true, nfd, NFD, nfc, NFC,
                          false);
NEON_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, true, nfkd, NFKD, nfkc, NFKC,
                          true);

#undef NEON_UTF16_IMPLEMENTATION

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
