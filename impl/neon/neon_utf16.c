// amalgamate add: #if UTF8NORM_IMPLEMENTATION_NEON

#include "impl/neon/neon_utf16.h"
#include "impl/neon.h"
#include "impl/neon/neon_common.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

NEON_UTF16_HELPERS(le, UTF8NORM_BIG_ENDIAN);
NEON_UTF16_HELPERS(be, !UTF8NORM_BIG_ENDIAN);

#undef NEON_UTF16_HELPERS

#define NEON_UTF16_IMPLEMENTATION(endianness, swap_endianness, decomp_form)    \
  /* Decompose UTF-16 code points that have some number of precomposed Hangul  \
   * syllables in them, but no table-based decompositions. */                  \
  static void neon_decompose_hangul_utf16##endianness##_##decomp_form(         \
      uint16x4_t in, uint32x4_t relevant, uint8_t **out, size_t out_length,    \
      uint8_t const *input, bool *end_is_cc) {                                 \
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
  static void neon_decompose_non_hangul_utf16##endianness##_##decomp_form(     \
      uint16x4_t in, uint32x4_t relevant, uint8_t **out, size_t out_length,    \
      uint8_t const *input, bool *end_is_cc) {                                 \
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
        input += 2;                                                            \
        nwritten = 2;                                                          \
        continue;                                                              \
      }                                                                        \
      if (last_is_cc && !is_cc) {                                              \
        scalar_sort_characters_utf16##endianness(*out, out_length);            \
      }                                                                        \
      last_is_cc = is_cc;                                                      \
      input += 2;                                                              \
      *out += nwritten;                                                        \
    }                                                                          \
    *end_is_cc = last_is_cc;                                                   \
  }                                                                            \
                                                                               \
  /* Decompose four UTF-16 code points in the BMP. */                          \
  static void neon_decompose_utf16##endianness##_##decomp_form(                \
      uint16x4_t in, uint8_t const *input, uint8_t **out, size_t out_length,   \
      bool *end_is_cc) {                                                       \
    uint32x4_t wide = vmovl_u32(in);                                           \
    uint32x4_t bloom = neon_evaluate_bloom_##decomp_form(wide);                \
    uint32x4_t hangul = neon_hangul_mask(wide);                                \
    bool bloom_result = vmaxvq_u32(bloom) > 0;                                 \
    bool hangul_result = vmaxvq_u32(hangul) > 0;                               \
    /* There are four cases:                                                   \
     * 1. No decomposable/combining or precomposed Hangul characters: skip     \
     * 2. Precomposed Hangul but no decomposable/combining characters          \
     * 3. Decomposable/combining but no precomposed Hangul characters          \
     * 4. Decomposable/combining AND precomposed Hangul characters: go scalar  \
     * */                                                                      \
    if (!bloom_result && !hangul_result) {                                     \
      uint16x8_t in_dummy = vcombine_u16(in, vdup_n_u16(0));                   \
      neon_skip_decomp_utf16##endianness(in_dummy, 8, out, out_length,         \
                                         end_is_cc);                           \
    } else if (hangul_result && !bloom_result) {                               \
      neon_decompose_hangul_utf16##endianness##_##decomp_form(                 \
          in, hangul, out, out_length, input, end_is_cc);                      \
    } else if (bloom_result && !hangul_result) {                               \
      neon_decompose_non_hangul_utf16##endianness##_##decomp_form(             \
          in, bloom, out, out_length, input, end_is_cc);                       \
    } else {                                                                   \
      *out +=                                                                  \
          scalar_normalize_utf16##endianness##_##decomp_form##_with_context(   \
              input, 8, *out, end_is_cc);                                      \
    }                                                                          \
  }                                                                            \
                                                                               \
  size_t neon_normalize_utf16##endianness##_##decomp_form(                     \
      uint8_t const *input, size_t length, uint8_t *out) {                     \
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
      uint16x8_t surrogates_mask =                                             \
          vceqq_u16(vandq_u16(in, vdupq_n_u16((uint16_t)0xF800)),              \
                    vdupq_n_u16((uint16_t)0xD800));                            \
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
        *out_ptr +=                                                            \
            scalar_normalize_utf16##endianness##_##decomp_form##_with_context( \
                input + p, 16, *out_ptr, &end_is_cc);                          \
      }                                                                        \
      p += 16;                                                                 \
    }                                                                          \
                                                                               \
    if (p < length) {                                                          \
      if (end_is_cc) {                                                         \
        scalar_sort_characters_utf16##endianness(*out_ptr, *out_ptr - start);  \
      }                                                                        \
      *out_ptr += scalar_normalize_utf16##endianness##_##decomp_form(          \
          input + p, length - p, *out_ptr);                                    \
    }                                                                          \
                                                                               \
    return *out_ptr - start;                                                   \
  }

NEON_UTF16_IMPLEMENTATION(le, UTF8NORM_BIG_ENDIAN, nfd);
NEON_UTF16_IMPLEMENTATION(le, UTF8NORM_BIG_ENDIAN, nfkd);
NEON_UTF16_IMPLEMENTATION(be, !UTF8NORM_BIG_ENDIAN, nfd);
NEON_UTF16_IMPLEMENTATION(be, !UTF8NORM_BIG_ENDIAN, nfkd);

#undef NEON_UTF16_IMPLEMENTATION

// amalgamate add: #endif // UTF8NORM_IMPLEMENTATION_NEON
