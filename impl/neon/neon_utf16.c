// amalgamate add: #if XXUTF_IMPLEMENTATION_NEON

#include "impl/neon/neon_utf16.h"
#include "impl/neon.h"
#include "impl/neon/neon_common.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Create a logical vector for high surrogates.
static inline uint16x8_t neon_make_surrogates_mask(uint16x8_t in) {
  return vandq_u16(vcleq_u16(in, vdupq_n_u16(0xDBFF)),
                   vcgeq_u16(in, vdupq_n_u16(0xD800)));
}

#define NEON_UTF16_HELPERS(endianness, swap_endianness)                        \
  /* Copy the input vector into the output buffer. */                          \
  static void neon_skip_decomp_utf16##endianness(                              \
      uint16x8_t in, size_t length, uint8_t **out, size_t out_length,          \
      bool *end_is_cc) {                                                       \
    if (*end_is_cc) {                                                          \
      scalar_sort_characters_utf16##endianness(*out, out_length);              \
    }                                                                          \
    *end_is_cc = false;                                                        \
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

#define NEON_UTF16_IMPLEMENTATION(endianness, swap_endianness, decomp_form,    \
                                  comp_form)                                   \
  /* Decompose UTF-16 code points that have some number of precomposed Hangul  \
   * syllables in them, but no table-based decompositions. */                  \
  static inline void neon_write_hangul_utf16##endianness##_##decomp_form(      \
      uint16x4_t in, uint32x4_t relevant, uint8_t **out, size_t out_length,    \
      const uint8_t *input, bool *end_is_cc) {                                 \
    if (*end_is_cc) {                                                          \
      scalar_sort_characters_utf16##endianness(*out, out_length);              \
    }                                                                          \
    *end_is_cc = false;                                                        \
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
      scalar_write_uint16_le(l, *out);                                         \
      *out += 2;                                                               \
      scalar_write_uint16_le(v, *out);                                         \
      *out += 2;                                                               \
      scalar_write_uint16_le(t, *out);                                         \
      *out += 2 * (t - NORMDATA_T_BASE > 0);                                   \
      input += 2;                                                              \
    }                                                                          \
  }                                                                            \
                                                                               \
  /* Decompose UTF-16 code points that have some number of precomposed or      \
   * combining characters in them, but no precomposed Hangul syllables. */     \
  static inline void neon_write_non_hangul_utf16##endianness##_##decomp_form(  \
      uint16x4_t in, uint32x4_t relevant, uint8_t **out, size_t out_length,    \
      const uint8_t *input, bool *end_is_cc) {                                 \
    bool last_is_cc = *end_is_cc;                                              \
    for (size_t i = 0; i < 4; i++) {                                           \
      uint16_t v = in[i];                                                      \
      bool r = relevant[i] > 0;                                                \
      bool is_cc = false;                                                      \
      size_t nwritten = 0;                                                     \
      /* This condition is met when the code point is not relevant, or if it   \
       * is relevant but no decomposition is found.  */                        \
      if (!r ||                                                                \
          (nwritten = scalar_decompose_utf16##endianness##_##decomp_form(      \
               v, *out, &is_cc)) == 0) {                                       \
        /* Copy the code point from the input */                               \
        (*out)[0] = input[0];                                                  \
        (*out)[1] = input[1];                                                  \
        nwritten = 2;                                                          \
      }                                                                        \
      if (last_is_cc && !is_cc) {                                              \
        scalar_sort_characters_utf16##endianness(*out, out_length);            \
      }                                                                        \
      last_is_cc = is_cc;                                                      \
      input += 2;                                                              \
      *out += nwritten;                                                        \
      out_length += nwritten;                                                  \
    }                                                                          \
    *end_is_cc = last_is_cc;                                                   \
  }                                                                            \
                                                                               \
  static inline void                                                           \
      neon_write_non_hangul_starters_utf16##endianness##_##decomp_form(        \
          uint16x4_t in, uint32x4_t relevant, uint8_t **out,                   \
          size_t out_length, const uint8_t *input, bool *end_is_cc) {          \
    if (*end_is_cc) {                                                          \
      scalar_sort_characters_utf16##endianness(*out, out_length);              \
    }                                                                          \
    *end_is_cc = false;                                                        \
    for (size_t i = 0; i < 4; i++) {                                           \
      uint16_t v = in[i];                                                      \
      bool r = relevant[i] > 0;                                                \
      bool is_cc = false;                                                      \
      size_t nwritten = 0;                                                     \
      /* This condition is met when the code point is not relevant, or if it   \
       * is relevant but no decomposition is found.  */                        \
      if (!r ||                                                                \
          (nwritten = scalar_decompose_utf16##endianness##_##decomp_form(      \
               v, *out, &is_cc)) == 0) {                                       \
        /* Copy the code point from the input */                               \
        (*out)[0] = input[0];                                                  \
        (*out)[1] = input[1];                                                  \
        nwritten = 2;                                                          \
      }                                                                        \
      input += 2;                                                              \
      *out += nwritten;                                                        \
    }                                                                          \
  }                                                                            \
                                                                               \
  /* Decompose four UTF-16 code points in the BMP. */                          \
  static void neon_decompose_utf16##endianness##_##decomp_form(                \
      uint16x4_t in, const uint8_t *input, uint8_t **out, size_t out_length,   \
      bool *end_is_cc) {                                                       \
    uint32x4_t wide = vmovl_u16(in);                                           \
    /* Get bloom results for both decomp relevant characters and non starter   \
     * characters. */                                                          \
    uint32x4x2_t bloom_results = neon_evaluate_bloom_##decomp_form(wide);      \
    uint32x4_t decomp_relevant = bloom_results.val[0];                         \
    uint32x4_t non_starter = bloom_results.val[1];                             \
    uint32x4_t hangul = neon_hangul_mask_u32(wide);                            \
    bool decomp_result = vmaxvq_u32(decomp_relevant) > 0;                      \
    bool non_starter_result = vmaxvq_u32(non_starter) > 0;                     \
    bool hangul_result = vmaxvq_u32(hangul) > 0;                               \
    if (likely(!decomp_result && !non_starter_result && !hangul_result)) {     \
      uint16x8_t in_dummy = vcombine_u16(in, vdup_n_u16(0));                   \
      neon_skip_decomp_utf16##endianness(in_dummy, 8, out, out_length,         \
                                         end_is_cc);                           \
      return;                                                                  \
    }                                                                          \
    if (likely(decomp_result && !non_starter_result && !hangul_result)) {      \
      neon_write_non_hangul_starters_utf16##endianness##_##decomp_form(        \
          in, decomp_relevant, out, out_length, input, end_is_cc);             \
      return;                                                                  \
    }                                                                          \
    /* At this point we just merge the two */                                  \
    uint32x4_t bloom = vorrq_u32(decomp_relevant, non_starter);                \
    bool bloom_result = vmaxvq_u32(bloom) > 0;                                 \
    /* There are three cases:                                                  \
     * 1. Decomposable/combining but no precomposed Hangul characters          \
     * 2. Precomposed Hangul but no decomposable/combining characters          \
     * 3. Decomposable/combining AND precomposed Hangul characters             \
     * */                                                                      \
    if (bloom_result && !hangul_result) {                                      \
      neon_write_non_hangul_utf16##endianness##_##decomp_form(                 \
          in, bloom, out, out_length, input, end_is_cc);                       \
    } else if (hangul_result && !bloom_result) {                               \
      neon_write_hangul_utf16##endianness##_##decomp_form(                     \
          in, hangul, out, out_length, input, end_is_cc);                      \
    } else {                                                                   \
      *out +=                                                                  \
          scalar_normalize_utf16##endianness##_##decomp_form##_with_context(   \
              input, 8, *out, out_length, end_is_cc);                          \
    }                                                                          \
  }                                                                            \
                                                                               \
  size_t neon_normalize_utf16##endianness##_##decomp_form(                     \
      const uint8_t *input, size_t length, uint8_t *out) {                     \
    uint8_t **out_ptr = &out;                                                  \
    uint8_t *start = out;                                                      \
                                                                               \
    const size_t SAFETY_MARGIN = 16;                                           \
    bool end_is_cc = false;                                                    \
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
        neon_skip_decomp_utf16##endianness(in, 16, out_ptr, *out_ptr - start,  \
                                           &end_is_cc);                        \
        p += 16;                                                               \
        continue;                                                              \
      }                                                                        \
      uint16x8_t surrogates_mask = neon_make_surrogates_mask(in);              \
      /* Check if we have no surrogate pairs */                                \
      if (vaddvq_u32(surrogates_mask) == 0) {                                  \
        uint16x4_t in1 = vget_low_u16(in);                                     \
        uint16x4_t in2 = vget_high_u16(in);                                    \
        /* Decompose the low code points and the high code points separately   \
         */                                                                    \
        neon_decompose_utf16##endianness##_##decomp_form(                      \
            in1, input + p, out_ptr, *out_ptr - start, &end_is_cc);            \
        neon_decompose_utf16##endianness##_##decomp_form(                      \
            in2, input + p + 8, out_ptr, *out_ptr - start, &end_is_cc);        \
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
                &end_is_cc);                                                   \
        /* Advance the input possibly by 2 more */                             \
        p += normalize_range - 16;                                             \
      }                                                                        \
      p += 16;                                                                 \
    }                                                                          \
                                                                               \
    if (p < length) {                                                          \
      if (end_is_cc) {                                                         \
        scalar_sort_characters_utf16##endianness(*out_ptr, *out_ptr - start);  \
      }                                                                        \
      bool dummy = false;                                                      \
      *out_ptr +=                                                              \
          scalar_normalize_utf16##endianness##_##decomp_form##_with_context(   \
              input + p, length - p, *out_ptr, *out_ptr - start, &dummy);      \
    }                                                                          \
                                                                               \
    return *out_ptr - start;                                                   \
  }                                                                            \
                                                                               \
  /* TODO: this looks a lot like the UTF-8 version... use macro? */            \
  static size_t neon_fallback_utf16##endianness##_##comp_form(                 \
      const uint8_t *input, const uint8_t *input_base, size_t input_length,    \
      uint8_t **out, size_t length) {                                          \
    size_t offset = input - input_base;                                        \
    /* Get the region that we will NFC normalize */                            \
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
    /* This is the position we will write to */                                \
    uint8_t *prev_out = *out - (offset - prev_starter);                        \
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
        continue;                                                              \
      }                                                                        \
      uint16x8_t surrogates_mask = neon_make_surrogates_mask(in);              \
      /* Check if we have no surrogate pairs */                                \
      if (vaddvq_u32(surrogates_mask) == 0) {                                  \
        uint16x4_t in1 = vget_low_u16(in);                                     \
        uint16x4_t in2 = vget_high_u16(in);                                    \
        uint32x4_t wide1 = vmovl_u16(in1);                                     \
        uint32x4_t wide2 = vmovl_u16(in2);                                     \
        uint32x4_t bloom1 = neon_evaluate_bloom_##comp_form(wide1);            \
        uint32x4_t bloom2 = neon_evaluate_bloom_##comp_form(wide2);            \
        bool irrelevant1 = vaddvq_u32(bloom1) == 0;                            \
        bool irrelevant2 = vaddvq_u32(bloom2) == 0;                            \
        if (irrelevant1 && irrelevant2) {                                      \
          vst1q_u8(*out_ptr, bytes);                                           \
          *out_ptr += 16;                                                      \
          p += 16;                                                             \
        } else if (irrelevant1) {                                              \
          uint8x8_t low_bytes = vget_low_u8(bytes);                            \
          vst1_u8(*out_ptr, low_bytes);                                        \
          *out_ptr += 8;                                                       \
          p += 8;                                                              \
        } else {                                                               \
          uint8_t first_relevant = neon_first_true(bloom1);                    \
          size_t copy_amount = first_relevant * 2;                             \
          uint8x8_t low_bytes = vget_low_u8(bytes);                            \
          vst1_u8(*out_ptr, low_bytes);                                        \
          *out_ptr += copy_amount;                                             \
          size_t normalized = neon_fallback_utf16##endianness##_##comp_form(   \
              input + p + copy_amount, input, length, out_ptr,                 \
              8 - copy_amount);                                                \
          p += copy_amount + normalized;                                       \
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
      }                                                                        \
    }                                                                          \
                                                                               \
    if (p < length) {                                                          \
      (void)neon_fallback_utf16##endianness##_##comp_form(                     \
          input + p, input, length, out_ptr, length - p);                      \
    }                                                                          \
                                                                               \
    return *out_ptr - start;                                                   \
  }

NEON_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, nfd, nfc);
NEON_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, nfkd, nfkc);
NEON_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, nfd, nfc);
NEON_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, nfkd, nfkc);

#undef NEON_UTF16_IMPLEMENTATION

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
