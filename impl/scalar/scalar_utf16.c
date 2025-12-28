#include "impl/scalar/scalar_utf16.h"
#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* TODO: this needs to be renamed */
void scalar_write_uint16_le(uint16_t x, uint8_t *out) {
  out[0] = (uint8_t)(x & 0xFF);
  out[1] = (uint8_t)(x >> 8);
}

void scalar_write_uint16_be(uint16_t x, uint8_t *out) {
  out[0] = (uint8_t)(x >> 8);
  out[1] = (uint8_t)(x & 0xFF);
}

uint16_t scalar_read_uint16le(const uint8_t *input) {
  return (uint16_t)input[0] | (uint16_t)input[1] << 8;
}

uint16_t scalar_read_uint16be(const uint8_t *input) {
  return ((uint16_t)input[0] << 8) | (uint16_t)input[1];
}

static inline size_t scalar_code_point_size_utf16(uint32_t code_point) {
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
  static size_t scalar_write_code_point_utf16##endianness(                     \
      uint32_t code_point, uint8_t *utf16_bytes) {                             \
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
  static uint32_t scalar_parse_code_point_utf16##endianness(                   \
      const uint8_t *input, uint8_t *size) {                                   \
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
  static size_t scalar_decompose_hangul_utf16##endianness(uint32_t code_point, \
                                                          uint8_t *out) {      \
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
  static uint32_t scalar_parse_code_point_utf16##endianness##_reverse(         \
      const uint8_t *input) {                                                  \
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
  size_t scalar_rfind_starter_utf16##endianness(const uint8_t *input,          \
                                                size_t length) {               \
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
  void scalar_print_code_points_utf16##endianness(const uint8_t *input,        \
                                                  size_t length) {             \
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
  void scalar_sort_characters_utf16##endianness(uint8_t *out, size_t length) { \
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

#define SCALAR_UTF16_IMPLEMENTATION(endianness, swap_endianness, decomp_form,       \
                                    decomp_form_upper, comp_form,                   \
                                    comp_form_upper)                                \
  static size_t                                                                     \
      scalar_decompose_utf16##endianness##_##decomp_form##_supplementary(           \
          uint32_t code_point, uint8_t *out, bool *is_cc) {                         \
    uint8_t *start = out;                                                           \
    uint32_t salt_hash = scalar_phash(                                              \
        code_point, 0, NORMDATA_##decomp_form_upper##_TABLE_SIZE);                  \
    uint32_t salt = NORMDATA_##decomp_form_upper##_SALT[salt_hash];                 \
    uint32_t key_hash = scalar_phash(                                               \
        code_point, salt, NORMDATA_##decomp_form_upper##_TABLE_SIZE);               \
    NormdataTableEntry kv = NORMDATA_##decomp_form_upper##_KV[key_hash];            \
    if (kv.k == code_point) {                                                       \
      uint32_t const *chars =                                                       \
          &NORMDATA_##decomp_form_upper##_CHARS[kv.offset];                         \
      for (size_t k = 0; k < kv.len; k++) {                                         \
        out += scalar_write_code_point_utf16##endianness(chars[k], out);            \
      }                                                                             \
      *is_cc = kv.ccc > 0;                                                          \
    } else {                                                                        \
      *is_cc = false;                                                               \
    }                                                                               \
                                                                                    \
    return out - start;                                                             \
  }                                                                                 \
                                                                                    \
  static size_t scalar_decompose_utf16##endianness##_##decomp_form##_bmp(           \
      uint32_t code_point, uint8_t *out, bool *is_cc) {                             \
    uint8_t *start = out;                                                           \
    uint16_t shift = code_point >> 6;                                               \
    uint16_t masked = code_point & 63;                                              \
    uint16_t index = NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[shift];        \
    uint32_t value =                                                                \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[index + masked];             \
    if (value == 0) {                                                               \
      *is_cc = false;                                                               \
      return 0;                                                                     \
    }                                                                               \
    uint8_t ccc = (value >> 16) & 0xFF;                                             \
    uint8_t length = (value >> 24) & 0xFF;                                          \
    assert(length % 2 == 0);                                                        \
    uint16_t offset = value & 0xFFFF;                                               \
    const uint8_t *bytes =                                                          \
        &NORMDATA_UTF16_##decomp_form_upper##_TRIE_DECOMPOSITIONS[offset];          \
    for (size_t k = 0; k < length; k += 2) {                                        \
      if (swap_endianness) {                                                        \
        out[0] = bytes[k + 1];                                                      \
        out[1] = bytes[k];                                                          \
      } else {                                                                      \
        out[0] = bytes[k];                                                          \
        out[1] = bytes[k + 1];                                                      \
      }                                                                             \
      out += 2;                                                                     \
    }                                                                               \
    *is_cc = ccc > 0;                                                               \
    return out - start;                                                             \
  }                                                                                 \
                                                                                    \
  size_t scalar_normalize_utf16##endianness##_##decomp_form##_with_context(         \
      const uint8_t *input, size_t length, uint8_t *out, size_t out_offset,         \
      bool *end_is_cc) {                                                            \
    uint8_t *start = out;                                                           \
                                                                                    \
    bool last_is_cc = *end_is_cc;                                                   \
    size_t p = 0;                                                                   \
    while (p < length) {                                                            \
      uint8_t size;                                                                 \
      uint32_t code_point =                                                         \
          scalar_parse_code_point_utf16##endianness(input + p, &size);              \
                                                                                    \
      bool is_cc = false;                                                           \
      uint8_t *c_start = out;                                                       \
      if (scalar_is_hangul(code_point)) {                                           \
        is_cc = false;                                                              \
        out += scalar_decompose_hangul_utf16##endianness(code_point, out);          \
      } else {                                                                      \
        size_t nwritten;                                                            \
        if (size == 2) {                                                            \
          nwritten = scalar_decompose_utf16##endianness##_##decomp_form##_bmp(      \
              code_point, out, &is_cc);                                             \
        } else {                                                                    \
          assert(size == 4);                                                        \
          nwritten =                                                                \
              scalar_decompose_utf16##endianness##_##decomp_form##_supplementary(   \
                  code_point, out, &is_cc);                                         \
        }                                                                           \
        if (nwritten == 0) {                                                        \
          /* Copy if no decomposition is found */                                   \
          for (uint8_t i = 0; i < size; i++) {                                      \
            *out++ = input[p + i];                                                  \
          }                                                                         \
        } else {                                                                    \
          out += nwritten;                                                          \
        }                                                                           \
      }                                                                             \
                                                                                    \
      p += size;                                                                    \
      /* Sort if the current character is a starter and the last character is       \
       * a non-starter. */                                                          \
      if (last_is_cc && !is_cc) {                                                   \
        scalar_sort_characters_utf16##endianness(c_start, (c_start - start) +       \
                                                              out_offset);          \
      }                                                                             \
      last_is_cc = is_cc;                                                           \
    }                                                                               \
                                                                                    \
    /* Sort on EOF */                                                               \
    if (last_is_cc) {                                                               \
      scalar_sort_characters_utf16##endianness(out,                                 \
                                               (out - start) + out_offset);         \
    }                                                                               \
    *end_is_cc = last_is_cc;                                                        \
    return out - start;                                                             \
  }                                                                                 \
                                                                                    \
  size_t scalar_normalize_utf16##endianness##_##decomp_form(                        \
      const uint8_t *input, size_t length, uint8_t *out) {                          \
    bool end_is_cc = false;                                                         \
    return scalar_normalize_utf16##endianness##_##decomp_form##_with_context(       \
        input, length, out, 0, &end_is_cc);                                         \
  }                                                                                 \
                                                                                    \
  size_t scalar_find_##comp_form##_irrelevant_starter_utf16##endianness(            \
      const uint8_t *input, size_t length) {                                        \
    uint32_t p = 0;                                                                 \
    while (p < length) {                                                            \
      uint8_t size;                                                                 \
      uint32_t c =                                                                  \
          scalar_parse_code_point_utf16##endianness(input + p, &size);              \
      uint8_t ccc = scalar_lookup_ccc(c);                                           \
      if (ccc == 0 && !scalar_is_##comp_form##_relevant(c)) {                       \
        return p;                                                                   \
      }                                                                             \
      p += size;                                                                    \
    }                                                                               \
    return (size_t)-1;                                                              \
  }                                                                                 \
                                                                                    \
  /* TODO: this has a lot of shared code with the utf8 version... consider          \
   * putting this logic into a shared macro... */                                   \
  size_t scalar_normalize_utf16##endianness##_##comp_form(                          \
      const uint8_t *input, size_t length, uint8_t *out) {                          \
    uint8_t *start = out;                                                           \
    size_t p = 0;                                                                   \
    uint8_t last_ccc = 0;                                                           \
                                                                                    \
    while (p < length) {                                                            \
      uint8_t size;                                                                 \
      uint32_t c =                                                                  \
          scalar_parse_code_point_utf16##endianness(input + p, &size);              \
                                                                                    \
      /* ASCII fast path to skip ccc lookup */                                      \
      if (c <= 0x7F) {                                                              \
        out[0] = input[p];                                                          \
        out[1] = input[p + 1];                                                      \
        out += 2;                                                                   \
        p += 2;                                                                     \
        last_ccc = 0;                                                               \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      uint8_t ccc = scalar_lookup_ccc(c);                                           \
                                                                                    \
      /* We can skip this character if it the combining classes are in the          \
       * right order and if it is irrelevant */                                     \
      if (ccc <= last_ccc && !scalar_is_##comp_form##_relevant(c)) {                \
        for (size_t i = 0; i < size; i++) {                                         \
          *out++ = input[p + i];                                                    \
        }                                                                           \
        p += size;                                                                  \
        last_ccc = ccc;                                                             \
        continue;                                                                   \
      }                                                                             \
                                                                                    \
      last_ccc = ccc;                                                               \
                                                                                    \
      /* This starter should be NF(K)C irrelevant */                                \
      size_t previous_starter_pos =                                                 \
          scalar_rfind_starter_utf16##endianness(input, p);                         \
      if (previous_starter_pos == (size_t)-1) {                                     \
        previous_starter_pos = 0;                                                   \
      }                                                                             \
                                                                                    \
      size_t next_irrelevant_starter_pos =                                          \
          scalar_find_##comp_form##_irrelevant_starter_utf16##endianness(           \
              input + p + size, length - p - size);                                 \
      if (next_irrelevant_starter_pos == (size_t)-1) {                              \
        next_irrelevant_starter_pos = length;                                       \
      } else {                                                                      \
        next_irrelevant_starter_pos += p + size;                                    \
      }                                                                             \
                                                                                    \
      /* NOTE: scary! */                                                            \
      uint8_t *normalized_out = out - (p - previous_starter_pos);                   \
      /* NF(K)D normalize a localized region in between the two starters that       \
       * are NF(K)C irrelevant. This guarantees that, if we NF(K)C normalize        \
       * this range, no characters after the end of the range in the input          \
       * will combine/interact with the range we normalized. In other words,        \
       * we run NF(K)C on the largest possible sub-range of characters that         \
       * may (or may not) have to do with the NF(K)C relevant character `c`         \
       * that we initially detected. */                                             \
      size_t normalized_length =                                                    \
          scalar_normalize_utf16##endianness##_##decomp_form(                       \
              input + previous_starter_pos,                                         \
              next_irrelevant_starter_pos - previous_starter_pos,                   \
              normalized_out);                                                      \
                                                                                    \
      size_t normalized_pos = 0;                                                    \
      uint8_t normalized_last_ccc = 255;                                            \
      /* Iterate through each code point, seeking back until a starter is           \
       * found and trying to combine with that. This part of the algorithm          \
       * closely matches up with the spec. See:                                     \
       * https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G49614 \
       */                                                                           \
      while (normalized_pos < normalized_length) {                                  \
        uint8_t normalized_size;                                                    \
        uint32_t normalized_c = scalar_parse_code_point_utf16##endianness(          \
            normalized_out + normalized_pos, &normalized_size);                     \
        uint8_t normalized_ccc = scalar_lookup_ccc(normalized_c);                   \
                                                                                    \
        /* Find the preceding starter. It should be composition irrelevant */       \
        /* TODO: we can cache this */                                               \
        size_t starter_pos = scalar_rfind_starter_utf16##endianness(                \
            normalized_out, normalized_pos);                                        \
        assert(starter_pos != normalized_pos);                                      \
        /* Skip if we don't have a starter before this */                           \
        if (starter_pos == (size_t)-1) {                                            \
          normalized_pos += normalized_size;                                        \
          normalized_last_ccc = normalized_ccc;                                     \
          continue;                                                                 \
        }                                                                           \
                                                                                    \
        uint8_t starter_size;                                                       \
        uint32_t starter = scalar_parse_code_point_utf16##endianness(               \
            normalized_out + starter_pos, &starter_size);                           \
        /* Skip if we're blocked from the starter */                                \
        if (normalized_ccc <= normalized_last_ccc &&                                \
            starter_pos + starter_size != normalized_pos) {                         \
          normalized_pos += normalized_size;                                        \
          normalized_last_ccc = normalized_ccc;                                     \
          continue;                                                                 \
        }                                                                           \
                                                                                    \
        uint32_t composed;                                                          \
        if (starter <= 0xFFFF && normalized_c <= 0xFFFF) {                          \
          composed = scalar_try_compose_bmp(starter, normalized_c);                 \
        } else {                                                                    \
          composed = normdata_compose_supplementary(starter, normalized_c);         \
        }                                                                           \
        /* Skip if no composed character */                                         \
        if (composed == 0) {                                                        \
          normalized_pos += normalized_size;                                        \
          normalized_last_ccc = normalized_ccc;                                     \
          continue;                                                                 \
        }                                                                           \
        uint8_t composed_size = scalar_code_point_size_utf16(composed);             \
        assert(composed_size >= starter_size);                                      \
                                                                                    \
        /* Shift left to delete the combining character */                          \
        scalar_shift_left(normalized_out + normalized_pos,                          \
                          normalized_length - normalized_pos,                       \
                          normalized_size);                                         \
        /* Account for combining character deletion */                              \
        normalized_length -= normalized_size;                                       \
                                                                                    \
        /* Shift everything right to make room for new composed code point */       \
        scalar_shift_right(normalized_out + starter_pos + starter_size,             \
                           normalized_length - starter_pos - starter_size,          \
                           composed_size - starter_size);                           \
        /* Overwrite the starter with the new composed code point */                \
        (void)scalar_write_code_point_utf16##endianness(                            \
            composed, normalized_out + starter_pos);                                \
        normalized_length += composed_size - starter_size;                          \
        normalized_pos += composed_size - starter_size;                             \
      }                                                                             \
                                                                                    \
      /* Set the out pointer to the end of the normalized buffer */                 \
      out = normalized_out + normalized_length;                                     \
      /* Set the input offset to the next starter that is garuanteed to not be      \
       * relevant to NF(K)C */                                                      \
      p = next_irrelevant_starter_pos;                                              \
    }                                                                               \
                                                                                    \
    return out - start;                                                             \
  }

SCALAR_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, nfd, NFD, nfc, NFC);
SCALAR_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, nfd, NFD, nfc, NFC);
SCALAR_UTF16_IMPLEMENTATION(le, XXUTF_BIG_ENDIAN, nfkd, NFKD, nfkc, NFKC);
SCALAR_UTF16_IMPLEMENTATION(be, !XXUTF_BIG_ENDIAN, nfkd, NFKD, nfkc, NFKC);

#undef SCALAR_UTF16_IMPLEMENTATION
