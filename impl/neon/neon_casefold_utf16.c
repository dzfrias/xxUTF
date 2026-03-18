// amalgamate add: #if XXUTF_IMPLEMENTATION_NEON

#include "impl/neon.h"
#include "impl/neon/neon_common.h"
#include "impl/scalar.h"
#include "normdata.h"
#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEON_UTF16_IMPLEMENTATION(endianness, swap_endianness, is_big_endian)  \
  size_t neon_casefold_utf16##endianness(const uint8_t *input, size_t length,  \
                                         uint8_t *out) {                       \
    uint8_t *start = out;                                                      \
                                                                               \
    const size_t SAFETY_MARGIN = 16;                                           \
    size_t p = 0;                                                              \
    while (p + SAFETY_MARGIN <= length) {                                      \
      uint8x16_t bytes = vld1q_u8(input + p);                                  \
      uint16x8_t in = vreinterpretq_u16_u8(bytes);                             \
      if (swap_endianness) {                                                   \
        in = vreinterpretq_u16_u8(vrev16q_u8(vreinterpretq_u8_u16(in)));       \
      }                                                                        \
      uint16x8_t ascii_mask = vcleq_u16(in, vdupq_n_u16(0x7F));                \
      /* ASCII fast path */                                                    \
      if (vminvq_u16(ascii_mask) != 0) {                                       \
        uint16x8_t shifted = vsubq_u16(in, vdupq_n_u16('A'));                  \
        uint16x8_t uppercase_mask =                                            \
            vcleq_u16(shifted, vdupq_n_u16('Z' - 'A'));                        \
        uint16x8_t casefold =                                                  \
            vbslq_u16(uppercase_mask, vaddq_u16(in, vdupq_n_u16(32)), in);     \
        if (swap_endianness) {                                                 \
          casefold = vreinterpretq_u16_u8(                                     \
              vrev16q_u8(vreinterpretq_u8_u16(casefold)));                     \
        }                                                                      \
        vst1q_u8(out, vreinterpretq_u8_u16(casefold));                         \
        out += 16;                                                             \
        p += 16;                                                               \
        continue;                                                              \
      }                                                                        \
      uint16x8_t surrogates_mask = neon_make_utf16_surrogates_mask(in);        \
      /* Check if we have all BMP characters */                                \
      if (vmaxvq_u32(surrogates_mask) == 0) {                                  \
        uint16x8_t values =                                                    \
            NEON_TRIE_LOOKUP_FULL(NORMDATA_UTF16_CASEFOLD_TRIE, in);           \
        for (size_t i = 0; i < 8; i++) {                                       \
          uint16_t value = values[i];                                          \
          if (value == 0) {                                                    \
            *out++ = input[p];                                                 \
            *out++ = input[p + 1];                                             \
            p += 2;                                                            \
            continue;                                                          \
          }                                                                    \
          uint8_t len = value >> 12;                                           \
          const uint8_t *casefold_offset =                                     \
              &NORMDATA_UTF16_CASEFOLD_DATA[value & 0xFFF];                    \
          uint8x8_t casefold_bytes = vld1_u8(casefold_offset);                 \
          if (is_big_endian) {                                                 \
            casefold_bytes = vrev16_u8(casefold_bytes);                        \
          }                                                                    \
          vst1_u8(out, casefold_bytes);                                        \
          out += len;                                                          \
          p += 2;                                                              \
        }                                                                      \
      } else {                                                                 \
        /* With surrogate pairs, we fall back to scalar */                     \
        size_t range = 16;                                                     \
        if (vgetq_lane_u16(surrogates_mask, 7) == 0xFFFF) {                    \
          range += 2;                                                          \
        }                                                                      \
        out += scalar_casefold_utf16##endianness(input + p, range, out);       \
        p += range;                                                            \
      }                                                                        \
    }                                                                          \
                                                                               \
    if (p < length) {                                                          \
      out += scalar_casefold_utf16##endianness(input + p, length - p, out);    \
    }                                                                          \
                                                                               \
    return out - start;                                                        \
  }                                                                            \
                                                                               \
  size_t neon_casefold_utf16##endianness##_length(const uint8_t *input,        \
                                                  size_t length) {             \
    size_t out_length = 0;                                                     \
    const size_t SAFETY_MARGIN = 16;                                           \
    size_t p = 0;                                                              \
    while (p + SAFETY_MARGIN <= length) {                                      \
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
      if (vmaxvq_u32(surrogates_mask) == 0) {                                  \
        uint16x8_t index = vshrq_n_u16(in, 6);                                 \
        uint16x8_t block_index = {                                             \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     0)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     1)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     2)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     3)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     4)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     5)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     6)],      \
            NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_INDEX[vgetq_lane_u16(index,    \
                                                                     7)],      \
        };                                                                     \
        uint16x8_t masked = vandq_u16(in, vdupq_n_u16(0x3F));                  \
        uint16x8_t data_offset = vaddq_u16(block_index, masked);               \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 0)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 1)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 2)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 3)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 4)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 5)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 6)];                                                  \
        out_length += NORMDATA_UTF16_CASEFOLD_LENGTH_TRIE_DATA[vgetq_lane_u16( \
            data_offset, 7)];                                                  \
        p += 16;                                                               \
      } else {                                                                 \
        size_t range = 16;                                                     \
        if (vgetq_lane_u16(surrogates_mask, 7) == 0xFFFF) {                    \
          range += 2;                                                          \
        }                                                                      \
        out_length +=                                                          \
            scalar_casefold_utf16##endianness##_length(input + p, range);      \
        p += range;                                                            \
      }                                                                        \
    }                                                                          \
    if (p < length) {                                                          \
      out_length +=                                                            \
          scalar_casefold_utf16##endianness##_length(input + p, length - p);   \
    }                                                                          \
    return out_length;                                                         \
  }

NEON_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, false);
NEON_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, true);

#undef NEON_UTF16_IMPLEMENTATION

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
