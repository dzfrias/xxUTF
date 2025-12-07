#include "impl/scalar/scalar_utf16.h"
#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline void scalar_write_uint16_le(uint16_t x, uint8_t *out) {
  out[0] = (uint8_t)(x & 0xFF);
  out[1] = (uint8_t)(x >> 8);
}

static inline void scalar_write_uint16_be(uint16_t x, uint8_t *out) {
  out[0] = (uint8_t)(x >> 8);
  out[1] = (uint8_t)(x & 0xFF);
}

static inline uint16_t scalar_read_uint16le(uint8_t const *input) {
  return (uint16_t)input[0] | (uint16_t)input[1] << 8;
}

static inline uint16_t scalar_read_uint16be(uint8_t const *input) {
  return ((uint16_t)input[0] << 8) | (uint16_t)input[1];
}

__attribute__((unused)) static inline size_t
scalar_code_point_size_utf16(uint32_t code_point) {
  return code_point <= 0xFFFF ? 2 : 4;
}

static inline bool scalar_is_utf16_low_surrogate(uint16_t code_unit) {
  return code_unit >= 0xDC00 && code_unit <= 0xDFFF;
}

static inline bool scalar_is_utf16_high_surrogate(uint16_t code_unit) {
  return code_unit >= 0xD800 && code_unit <= 0xDBFF;
}

#define SCALAR_UTF16_HELPERS(endianness)                                       \
  /* Write a code point into the output buffer as UTF-16 bytes. Returns the    \
   * number of bytes written. */                                               \
  __attribute__((unused)) static size_t                                        \
      scalar_write_code_point_utf16##endianness(uint32_t code_point,           \
                                                uint8_t *utf16_bytes) {        \
    /* Check if in BMP */                                                      \
    if (code_point <= 0xFFFF) {                                                \
      uint16_t u = (uint16_t)code_point;                                       \
      scalar_write_uint16_##endianness(u, utf16_bytes);                        \
      return 2;                                                                \
    }                                                                          \
    code_point -= 0x10000;                                                     \
    uint16_t high = 0xD800 | (code_point >> 10);                               \
    uint16_t low = 0xDC00 | (code_point & 0x3FF);                              \
    scalar_write_uint16_##endianness(high, utf16_bytes);                       \
    scalar_write_uint16_##endianness(low, utf16_bytes + 2);                    \
    return 4;                                                                  \
  }                                                                            \
                                                                               \
  /* Parse a UTF-16 code point from a byte buffer. */                          \
  __attribute__((unused)) static uint32_t                                      \
      scalar_parse_code_point_utf16##endianness(uint8_t const *input,          \
                                                uint8_t *size) {               \
    uint16_t w1 = scalar_read_uint16##endianness(input);                       \
    if (scalar_is_utf16_high_surrogate(w1)) {                                  \
      uint16_t w2 = scalar_read_uint16##endianness(input + 2);                 \
      uint32_t cp =                                                            \
          (((uint32_t)(w1 - 0xD800) << 10) | ((uint32_t)(w2 - 0xDC00))) +      \
          0x10000;                                                             \
      *size = 4;                                                               \
      return cp;                                                               \
    }                                                                          \
    *size = 2;                                                                 \
    return w1;                                                                 \
  }                                                                            \
                                                                               \
  /* Decompose a Hangul code point into UTF-16 */                              \
  __attribute__((unused)) static size_t                                        \
      scalar_decompose_hangul_utf16##endianness(uint32_t code_point,           \
                                                uint8_t *out) {                \
    uint32_t s_index = code_point - NORMDATA_S_BASE;                           \
    uint32_t l_index = s_index / NORMDATA_N_COUNT;                             \
    uint32_t v_index = (s_index % NORMDATA_N_COUNT) / NORMDATA_T_COUNT;        \
    uint32_t t_index = s_index % NORMDATA_T_COUNT;                             \
                                                                               \
    size_t nwritten = 0;                                                       \
    nwritten += scalar_write_code_point_utf16##endianness(                     \
        NORMDATA_L_BASE + l_index, out);                                       \
    nwritten += scalar_write_code_point_utf16##endianness(                     \
        NORMDATA_V_BASE + v_index, out + nwritten);                            \
    if (t_index > 0) {                                                         \
      nwritten += scalar_write_code_point_utf16##endianness(                   \
          NORMDATA_T_BASE + t_index, out + nwritten);                          \
    }                                                                          \
    return nwritten;                                                           \
  }                                                                            \
                                                                               \
  /* Parse a UTF-16 encoded code point in reverse. */                          \
  __attribute__((unused)) static uint32_t                                      \
      scalar_parse_code_point_utf16##endianness##_reverse(                     \
          uint8_t const *input) {                                              \
    uint16_t code_unit = scalar_read_uint16##endianness(input);                \
    uint32_t code_point = 0;                                                   \
    if (scalar_is_utf16_low_surrogate(code_unit)) {                            \
      /* If we found a low surrogate, parse the matching high surrogate */     \
      uint8_t size;                                                            \
      code_point =                                                             \
          scalar_parse_code_point_utf16##endianness(input - 2, &size);         \
    } else {                                                                   \
      /* In this case, should be BMP */                                        \
      code_point = code_unit;                                                  \
    }                                                                          \
    return code_point;                                                         \
  }                                                                            \
                                                                               \
  /* Find a starter character in a UTF-16 buffer, searching from right to left \
   */                                                                          \
  __attribute__((unused)) static size_t                                        \
      scalar_rfind_starter_utf16##endianness(const uint8_t *input,             \
                                             size_t length) {                  \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint32_t c = scalar_parse_code_point_utf16##endianness##_reverse(        \
          input + (length - p - 2));                                           \
      if (c > 0xFFFF) {                                                        \
        p += 2;                                                                \
      }                                                                        \
      uint8_t ccc = scalar_lookup_ccc(c);                                      \
      if (ccc == 0) {                                                          \
        return length - p - 2;                                                 \
      }                                                                        \
      p += 2;                                                                  \
    }                                                                          \
    return -1;                                                                 \
  }                                                                            \
                                                                               \
  __attribute__((unused)) static void                                          \
      scalar_print_code_points_utf16##endianness(const uint8_t *input,         \
                                                 size_t length) {              \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint8_t size;                                                            \
      uint32_t c =                                                             \
          scalar_parse_code_point_utf16##endianness(input + p, &size);         \
      printf("%u(p=%zu) ", c, p);                                              \
      p += size;                                                               \
    }                                                                          \
    printf("\n");                                                              \
  }                                                                            \
                                                                               \
  /* Sort combining characters in-place (implementation of the canonical       \
   * ordering algorithm), starting at the end of the buffer and working        \
   * backwards. */                                                             \
  __attribute__((unused)) static void                                          \
      scalar_sort_characters_utf16##endianness(uint8_t *out, size_t length) {  \
    if (length == 0) {                                                         \
      return;                                                                  \
    }                                                                          \
                                                                               \
    uint8_t *start = out;                                                      \
                                                                               \
    uint8_t last_ccc = 255;                                                    \
    bool needs_sort = false;                                                   \
    out -= 2;                                                                  \
    while ((size_t)(start - out) <= length) {                                  \
      uint32_t code_point =                                                    \
          scalar_parse_code_point_utf16##endianness##_reverse(out);            \
      if (code_point > 0xFFFF) {                                               \
        out -= 2;                                                              \
      }                                                                        \
      uint8_t ccc = scalar_lookup_ccc(code_point);                             \
      if (last_ccc < ccc) {                                                    \
        needs_sort = true;                                                     \
      }                                                                        \
      /* Walk back until we have found the last starter */                     \
      if (ccc == 0) {                                                          \
        break;                                                                 \
      }                                                                        \
      out -= 2;                                                                \
      last_ccc = ccc;                                                          \
    }                                                                          \
                                                                               \
    /* Fast path for when the buffer is already sorted */                      \
    if (!needs_sort) {                                                         \
      return;                                                                  \
    }                                                                          \
                                                                               \
    if ((size_t)(start - out) > length) {                                      \
      out += 2;                                                                \
    }                                                                          \
                                                                               \
    size_t n = start - out;                                                    \
    while (true) {                                                             \
      bool did_swap = false;                                                   \
      uint8_t last_size;                                                       \
      for (size_t j = 0; j < n; j += last_size) {                              \
        uint8_t size1;                                                         \
        uint8_t size2;                                                         \
        uint32_t c1 =                                                          \
            scalar_parse_code_point_utf16##endianness(out + j, &size1);        \
        if (j + size1 >= n) {                                                  \
          break;                                                               \
        }                                                                      \
        uint32_t c2 = scalar_parse_code_point_utf16##endianness(               \
            out + j + size1, &size2);                                          \
        uint8_t ccc1 = scalar_lookup_ccc(c1);                                  \
        uint8_t ccc2 = scalar_lookup_ccc(c2);                                  \
        last_size = size1;                                                     \
        if (ccc1 > ccc2) {                                                     \
          scalar_rotate(out + j, size1 + size2, size2);                        \
          last_size = size2;                                                   \
          did_swap = true;                                                     \
        }                                                                      \
      }                                                                        \
      if (!did_swap) {                                                         \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
  }

SCALAR_UTF16_HELPERS(le);
SCALAR_UTF16_HELPERS(be);

#undef SCALAR_UTF16_HELPERS

#define SCALAR_UTF16_IMPLEMENTATION(endianness, form, form_upper)              \
  static size_t scalar_decompose_utf16##endianness##_##form(                   \
      uint32_t code_point, uint8_t *out, bool *is_cc) {                        \
    uint8_t *start = out;                                                      \
    uint32_t salt_hash =                                                       \
        scalar_phash(code_point, 0, NORMDATA_##form_upper##_TABLE_SIZE);       \
    uint32_t salt = NORMDATA_##form_upper##_SALT[salt_hash];                   \
    uint32_t key_hash =                                                        \
        scalar_phash(code_point, salt, NORMDATA_##form_upper##_TABLE_SIZE);    \
    NormdataTableEntry kv = NORMDATA_##form_upper##_KV[key_hash];              \
    if (kv.k == code_point) {                                                  \
      uint32_t const *chars = &NORMDATA_##form_upper##_CHARS[kv.offset];       \
      for (size_t k = 0; k < kv.len; k++) {                                    \
        out += scalar_write_code_point_utf16##endianness(chars[k], out);       \
      }                                                                        \
      *is_cc = kv.ccc > 0;                                                     \
    } else {                                                                   \
      *is_cc = false;                                                          \
    }                                                                          \
                                                                               \
    return out - start;                                                        \
  }                                                                            \
                                                                               \
  size_t scalar_normalize_utf16##endianness##_##form##_with_context(           \
      uint8_t const *input, size_t length, uint8_t *out, bool *end_is_cc) {    \
    uint8_t *start = out;                                                      \
                                                                               \
    bool last_is_cc = *end_is_cc;                                              \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint8_t size;                                                            \
      uint32_t code_point =                                                    \
          scalar_parse_code_point_utf16##endianness(input + p, &size);         \
                                                                               \
      bool is_cc = false;                                                      \
      uint8_t *c_start = out;                                                  \
      if (scalar_is_hangul(code_point)) {                                      \
        is_cc = false;                                                         \
        out += scalar_decompose_hangul_utf16##endianness(code_point, out);     \
      } else {                                                                 \
        size_t nwritten = scalar_decompose_utf16##endianness##_##form(         \
            code_point, out, &is_cc);                                          \
        if (nwritten == 0) {                                                   \
          /* Copy if no decomposition is found */                              \
          for (uint8_t i = 0; i < size; i++) {                                 \
            *out++ = input[p + i];                                             \
          }                                                                    \
        } else {                                                               \
          out += nwritten;                                                     \
        }                                                                      \
      }                                                                        \
                                                                               \
      p += size;                                                               \
      /* Sort if the current character is a starter and the last character is  \
       * a non-starter. */                                                     \
      if (last_is_cc && !is_cc) {                                              \
        scalar_sort_characters_utf16##endianness(c_start, c_start - start);    \
      }                                                                        \
      last_is_cc = is_cc;                                                      \
    }                                                                          \
                                                                               \
    /* Sort on EOF */                                                          \
    if (last_is_cc) {                                                          \
      scalar_sort_characters_utf16##endianness(out, out - start);              \
    }                                                                          \
    *end_is_cc = last_is_cc;                                                   \
    return out - start;                                                        \
  }                                                                            \
                                                                               \
  size_t scalar_normalize_utf16##endianness##_##form(                          \
      const uint8_t *input, size_t length, uint8_t *out) {                     \
    bool end_is_cc = false;                                                    \
    return scalar_normalize_utf16##endianness##_##form##_with_context(         \
        input, length, out, &end_is_cc);                                       \
  }

SCALAR_UTF16_IMPLEMENTATION(le, nfd, NFD);
SCALAR_UTF16_IMPLEMENTATION(be, nfd, NFD);
SCALAR_UTF16_IMPLEMENTATION(le, nfkd, NFKD);
SCALAR_UTF16_IMPLEMENTATION(be, nfkd, NFKD);

#undef SCALAR_UTF16_IMPLEMENTATION
