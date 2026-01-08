#include "impl/scalar.h"
#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCALAR_UTF16_HELPERS(endianness)                                       \
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
  /* Sort combining characters in-place (implementation of the canonical       \
   * ordering algorithm), starting at the end of the buffer and working        \
   * backwards. */                                                             \
  uint8_t scalar_sort_characters_utf16##endianness(uint8_t *out,               \
                                                   size_t length) {            \
    if (length == 0) {                                                         \
      return 0;                                                                \
    }                                                                          \
                                                                               \
    uint8_t *start = out;                                                      \
    uint8_t final_ccc = 255;                                                   \
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
      if (final_ccc == 255) {                                                  \
        final_ccc = ccc;                                                       \
      }                                                                        \
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
      return final_ccc;                                                        \
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
          if (j + size1 + size2 == n) {                                        \
            final_ccc = ccc1;                                                  \
          }                                                                    \
        }                                                                      \
      }                                                                        \
      if (!did_swap) {                                                         \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    return final_ccc;                                                          \
  }

SCALAR_UTF16_HELPERS(le);
SCALAR_UTF16_HELPERS(be);

#undef SCALAR_UTF16_HELPERS

#define SCALAR_UTF16_IMPLEMENTATION(endianness, is_big_endian, decomp_form,                \
                                    decomp_form_upper, comp_form,                          \
                                    comp_form_upper)                                       \
  static size_t                                                                            \
      scalar_decompose_utf16##endianness##_##decomp_form##_supplementary(                  \
          uint32_t code_point, uint8_t *out, uint8_t *ccc) {                               \
    uint8_t *start = out;                                                                  \
    uint32_t salt_hash = scalar_phash(                                                     \
        code_point, 0, NORMDATA_##decomp_form_upper##_TABLE_SIZE);                         \
    uint32_t salt = NORMDATA_##decomp_form_upper##_SALT[salt_hash];                        \
    uint32_t key_hash = scalar_phash(                                                      \
        code_point, salt, NORMDATA_##decomp_form_upper##_TABLE_SIZE);                      \
    NormdataTableEntry kv = NORMDATA_##decomp_form_upper##_KV[key_hash];                   \
    if (kv.k == code_point) {                                                              \
      uint32_t const *chars =                                                              \
          &NORMDATA_##decomp_form_upper##_CHARS[kv.offset];                                \
      for (size_t k = 0; k < kv.len; k++) {                                                \
        out += scalar_write_code_point_utf16##endianness(chars[k], out);                   \
      }                                                                                    \
      *ccc = kv.last_ccc;                                                                  \
    } else {                                                                               \
      *ccc = 0;                                                                            \
    }                                                                                      \
                                                                                           \
    return out - start;                                                                    \
  }                                                                                        \
                                                                                           \
  static size_t scalar_decompose_utf16##endianness##_##decomp_form##_bmp(                  \
      uint32_t code_point, uint8_t *out, uint8_t *ccc) {                                   \
    uint8_t *start = out;                                                                  \
    uint16_t shift = code_point >> 6;                                                      \
    uint16_t masked = code_point & 63;                                                     \
    uint16_t index = NORMDATA_UTF16_##decomp_form_upper##_TRIE_INDEX[shift];               \
    uint32_t value =                                                                       \
        NORMDATA_UTF16_##decomp_form_upper##_TRIE_DATA[index + masked];                    \
    if (value == 0) {                                                                      \
      *ccc = 0;                                                                            \
      return 0;                                                                            \
    }                                                                                      \
    *ccc = (value >> 16) & 0xFF;                                                           \
    uint8_t length = (value >> 24) & 0xFF;                                                 \
    assert(length % 2 == 0);                                                               \
    uint16_t offset = value & 0xFFFF;                                                      \
    const uint8_t *bytes =                                                                 \
        &NORMDATA_UTF16_##decomp_form_upper##_TRIE_DECOMPOSITIONS[offset];                 \
    for (size_t k = 0; k < length; k += 2) {                                               \
      if (is_big_endian) {                                                                 \
        out[0] = bytes[k + 1];                                                             \
        out[1] = bytes[k];                                                                 \
      } else {                                                                             \
        out[0] = bytes[k];                                                                 \
        out[1] = bytes[k + 1];                                                             \
      }                                                                                    \
      out += 2;                                                                            \
    }                                                                                      \
    return out - start;                                                                    \
  }                                                                                        \
                                                                                           \
  size_t scalar_find_first_stable_utf16##endianness##_##decomp_form(                       \
      const uint8_t *input, size_t length) {                                               \
    uint32_t p = 0;                                                                        \
    while (p < length) {                                                                   \
      uint8_t size;                                                                        \
      uint32_t c =                                                                         \
          scalar_parse_code_point_utf16##endianness(input + p, &size);                     \
      uint8_t ccc = scalar_lookup_ccc(c);                                                  \
      if (ccc == 0 && !scalar_is_##decomp_form##_relevant(c)) {                            \
        return p;                                                                          \
      }                                                                                    \
      p += size;                                                                           \
    }                                                                                      \
    return (size_t)-1;                                                                     \
  }                                                                                        \
                                                                                           \
  size_t scalar_find_last_stable_utf16##endianness##_##decomp_form(                        \
      const uint8_t *input, size_t length) {                                               \
    size_t cutoff = length;                                                                \
    while (cutoff > 0) {                                                                   \
      cutoff = scalar_rfind_starter_utf16##endianness(input, cutoff);                      \
      if (cutoff == (size_t)-1) {                                                          \
        return (size_t)-1;                                                                 \
      }                                                                                    \
      uint8_t size;                                                                        \
      uint32_t c =                                                                         \
          scalar_parse_code_point_utf16##endianness(input + cutoff, &size);                \
      uint8_t ccc = scalar_lookup_ccc(c);                                                  \
      if (ccc == 0 && !scalar_is_##decomp_form##_relevant(c)) {                            \
        return cutoff;                                                                     \
      }                                                                                    \
    }                                                                                      \
    return (size_t)-1;                                                                     \
  }                                                                                        \
                                                                                           \
  size_t scalar_normalize_utf16##endianness##_##decomp_form##_with_context(                \
      const uint8_t *input, size_t length, uint8_t *out, size_t out_offset,                \
      uint8_t *last_ccc) {                                                                 \
    uint8_t *start = out;                                                                  \
                                                                                           \
    size_t p = 0;                                                                          \
    while (p < length) {                                                                   \
      uint8_t size;                                                                        \
      uint32_t code_point =                                                                \
          scalar_parse_code_point_utf16##endianness(input + p, &size);                     \
                                                                                           \
      uint8_t ccc = 0;                                                                     \
      if (scalar_is_hangul(code_point)) {                                                  \
        out += scalar_decompose_hangul_utf16##endianness(code_point, out);                 \
      } else {                                                                             \
        size_t nwritten;                                                                   \
        if (size == 2) {                                                                   \
          nwritten = scalar_decompose_utf16##endianness##_##decomp_form##_bmp(             \
              code_point, out, &ccc);                                                      \
        } else {                                                                           \
          assert(size == 4);                                                               \
          nwritten =                                                                       \
              scalar_decompose_utf16##endianness##_##decomp_form##_supplementary(          \
                  code_point, out, &ccc);                                                  \
        }                                                                                  \
        if (nwritten == 0) {                                                               \
          /* Copy if no decomposition is found */                                          \
          for (uint8_t i = 0; i < size; i++) {                                             \
            *out++ = input[p + i];                                                         \
          }                                                                                \
        } else {                                                                           \
          out += nwritten;                                                                 \
        }                                                                                  \
      }                                                                                    \
                                                                                           \
      p += size;                                                                           \
      if (ccc != 0 && *last_ccc > ccc) {                                                   \
        ccc = scalar_sort_characters_utf16##endianness(out, (out - start) +                \
                                                                out_offset);               \
      }                                                                                    \
      *last_ccc = ccc;                                                                     \
    }                                                                                      \
                                                                                           \
    return out - start;                                                                    \
  }                                                                                        \
                                                                                           \
  size_t scalar_normalize_utf16##endianness##_##decomp_form(                               \
      const uint8_t *input, size_t length, uint8_t *out) {                                 \
    uint8_t last_ccc = 0;                                                                  \
    return scalar_normalize_utf16##endianness##_##decomp_form##_with_context(              \
        input, length, out, 0, &last_ccc);                                                 \
  }                                                                                        \
                                                                                           \
  size_t scalar_find_first_stable_utf16##endianness##_##comp_form(                         \
      const uint8_t *input, size_t length) {                                               \
    uint32_t p = 0;                                                                        \
    while (p < length) {                                                                   \
      uint8_t size;                                                                        \
      uint32_t c =                                                                         \
          scalar_parse_code_point_utf16##endianness(input + p, &size);                     \
      uint8_t ccc = scalar_lookup_ccc(c);                                                  \
      if (ccc == 0 && !scalar_is_##comp_form##_relevant(c)) {                              \
        return p;                                                                          \
      }                                                                                    \
      p += size;                                                                           \
    }                                                                                      \
    return (size_t)-1;                                                                     \
  }                                                                                        \
                                                                                           \
  size_t scalar_find_last_stable_utf16##endianness##_##comp_form(                          \
      const uint8_t *input, size_t length) {                                               \
    size_t cutoff = length;                                                                \
    while (cutoff > 0) {                                                                   \
      cutoff = scalar_rfind_starter_utf16##endianness(input, cutoff);                      \
      if (cutoff == (size_t)-1) {                                                          \
        return (size_t)-1;                                                                 \
      }                                                                                    \
      uint8_t size;                                                                        \
      uint32_t c =                                                                         \
          scalar_parse_code_point_utf16##endianness(input + cutoff, &size);                \
      uint8_t ccc = scalar_lookup_ccc(c);                                                  \
      if (ccc == 0 && !scalar_is_##comp_form##_relevant(c)) {                              \
        return cutoff;                                                                     \
      }                                                                                    \
    }                                                                                      \
    return (size_t)-1;                                                                     \
  }                                                                                        \
                                                                                           \
  /* TODO: this has a lot of shared code with the utf8 version... consider                 \
   * putting this logic into a shared macro... */                                          \
  size_t scalar_normalize_utf16##endianness##_##comp_form(                                 \
      const uint8_t *input, size_t length, uint8_t *out) {                                 \
    uint8_t *start = out;                                                                  \
    size_t p = 0;                                                                          \
    uint8_t last_ccc = 0;                                                                  \
                                                                                           \
    while (p < length) {                                                                   \
      uint8_t size;                                                                        \
      uint32_t c =                                                                         \
          scalar_parse_code_point_utf16##endianness(input + p, &size);                     \
                                                                                           \
      /* ASCII fast path to skip ccc lookup */                                             \
      if (c <= 0x7F) {                                                                     \
        out[0] = input[p];                                                                 \
        out[1] = input[p + 1];                                                             \
        out += 2;                                                                          \
        p += 2;                                                                            \
        last_ccc = 0;                                                                      \
        continue;                                                                          \
      }                                                                                    \
                                                                                           \
      uint8_t ccc = scalar_lookup_ccc(c);                                                  \
                                                                                           \
      /* We can skip this character if it the combining classes are in the                 \
       * right order and if it is irrelevant */                                            \
      if (ccc <= last_ccc && !scalar_is_##comp_form##_relevant(c)) {                       \
        for (size_t i = 0; i < size; i++) {                                                \
          *out++ = input[p + i];                                                           \
        }                                                                                  \
        p += size;                                                                         \
        last_ccc = ccc;                                                                    \
        continue;                                                                          \
      }                                                                                    \
                                                                                           \
      last_ccc = ccc;                                                                      \
                                                                                           \
      /* This starter should be NF(K)C irrelevant */                                       \
      size_t previous_starter_pos =                                                        \
          scalar_rfind_starter_utf16##endianness(input, p);                                \
      if (previous_starter_pos == (size_t)-1) {                                            \
        previous_starter_pos = 0;                                                          \
      }                                                                                    \
                                                                                           \
      size_t next_irrelevant_starter_pos =                                                 \
          scalar_find_first_stable_utf16##endianness##_##comp_form(                        \
              input + p + size, length - p - size);                                        \
      if (next_irrelevant_starter_pos == (size_t)-1) {                                     \
        next_irrelevant_starter_pos = length;                                              \
      } else {                                                                             \
        next_irrelevant_starter_pos += p + size;                                           \
      }                                                                                    \
                                                                                           \
      /* NOTE: scary! */                                                                   \
      uint8_t *normalized_out = out - (p - previous_starter_pos);                          \
      /* NF(K)D normalize a localized region in between the two starters that              \
       * are NF(K)C irrelevant. This guarantees that, if we NF(K)C normalize               \
       * this range, no characters after the end of the range in the input                 \
       * will combine/interact with the range we normalized. In other words,               \
       * we run NF(K)C on the largest possible sub-range of characters that                \
       * may (or may not) have to do with the NF(K)C relevant character `c`                \
       * that we initially detected. */                                                    \
      size_t normalized_length =                                                           \
          scalar_normalize_utf16##endianness##_##decomp_form(                              \
              input + previous_starter_pos,                                                \
              next_irrelevant_starter_pos - previous_starter_pos,                          \
              normalized_out);                                                             \
                                                                                           \
      size_t normalized_pos = 0;                                                           \
      uint8_t normalized_last_ccc = 255;                                                   \
      /* Iterate through each code point, seeking back until a starter is                  \
       * found and trying to combine with that. This part of the algorithm                 \
       * closely matches up with the spec. See:                                            \
       * https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G49614        \
       */                                                                                  \
      while (normalized_pos < normalized_length) {                                         \
        uint8_t normalized_size;                                                           \
        uint32_t normalized_c = scalar_parse_code_point_utf16##endianness(                 \
            normalized_out + normalized_pos, &normalized_size);                            \
        uint8_t normalized_ccc = scalar_lookup_ccc(normalized_c);                          \
                                                                                           \
        /* Find the preceding starter. It should be composition irrelevant */              \
        /* TODO: we can cache this */                                                      \
        size_t starter_pos = scalar_rfind_starter_utf16##endianness(                       \
            normalized_out, normalized_pos);                                               \
        assert(starter_pos != normalized_pos);                                             \
        /* Skip if we don't have a starter before this */                                  \
        if (starter_pos == (size_t)-1) {                                                   \
          normalized_pos += normalized_size;                                               \
          normalized_last_ccc = normalized_ccc;                                            \
          continue;                                                                        \
        }                                                                                  \
                                                                                           \
        uint8_t starter_size;                                                              \
        uint32_t starter = scalar_parse_code_point_utf16##endianness(                      \
            normalized_out + starter_pos, &starter_size);                                  \
        /* Skip if we're blocked from the starter */                                       \
        if (normalized_ccc <= normalized_last_ccc &&                                       \
            starter_pos + starter_size != normalized_pos) {                                \
          normalized_pos += normalized_size;                                               \
          normalized_last_ccc = normalized_ccc;                                            \
          continue;                                                                        \
        }                                                                                  \
                                                                                           \
        uint32_t composed;                                                                 \
        if (starter <= 0xFFFF && normalized_c <= 0xFFFF) {                                 \
          composed = scalar_try_compose_bmp(starter, normalized_c);                        \
        } else {                                                                           \
          composed = normdata_compose_supplementary(starter, normalized_c);                \
        }                                                                                  \
        /* Skip if no composed character */                                                \
        if (composed == 0) {                                                               \
          normalized_pos += normalized_size;                                               \
          normalized_last_ccc = normalized_ccc;                                            \
          continue;                                                                        \
        }                                                                                  \
        uint8_t composed_size = scalar_code_point_size_utf16(composed);                    \
        assert(composed_size >= starter_size);                                             \
                                                                                           \
        /* Shift left to delete the combining character */                                 \
        scalar_shift_left(normalized_out + normalized_pos,                                 \
                          normalized_length - normalized_pos,                              \
                          normalized_size);                                                \
        /* Account for combining character deletion */                                     \
        normalized_length -= normalized_size;                                              \
                                                                                           \
        /* Shift everything right to make room for new composed code point */              \
        scalar_shift_right(normalized_out + starter_pos + starter_size,                    \
                           normalized_length - starter_pos - starter_size,                 \
                           composed_size - starter_size);                                  \
        /* Overwrite the starter with the new composed code point */                       \
        (void)scalar_write_code_point_utf16##endianness(                                   \
            composed, normalized_out + starter_pos);                                       \
        normalized_length += composed_size - starter_size;                                 \
        normalized_pos += composed_size - starter_size;                                    \
      }                                                                                    \
                                                                                           \
      /* Set the out pointer to the end of the normalized buffer */                        \
      out = normalized_out + normalized_length;                                            \
      /* Set the input offset to the next starter that is garuanteed to not be             \
       * relevant to NF(K)C */                                                             \
      p = next_irrelevant_starter_pos;                                                     \
    }                                                                                      \
                                                                                           \
    return out - start;                                                                    \
  }                                                                                        \
                                                                                           \
  static uint8_t                                                                           \
      scalar_decomposition_length_utf16##endianness##_##decomp_form##_supplementary(       \
          uint32_t code_point) {                                                           \
    uint32_t salt_hash = scalar_phash(                                                     \
        code_point, 0, NORMDATA_##decomp_form_upper##_TABLE_SIZE);                         \
    uint32_t salt = NORMDATA_##decomp_form_upper##_SALT[salt_hash];                        \
    uint32_t key_hash = scalar_phash(                                                      \
        code_point, salt, NORMDATA_##decomp_form_upper##_TABLE_SIZE);                      \
    NormdataTableEntry kv = NORMDATA_##decomp_form_upper##_KV[key_hash];                   \
    if (kv.k == code_point) {                                                              \
      uint32_t const *chars =                                                              \
          &NORMDATA_##decomp_form_upper##_CHARS[kv.offset];                                \
      uint8_t length = 0;                                                                  \
      for (size_t k = 0; k < kv.len; k++) {                                                \
        length += scalar_code_point_size_utf16(chars[k]);                                  \
      }                                                                                    \
      return length;                                                                       \
    }                                                                                      \
    return 4;                                                                              \
  }                                                                                        \
                                                                                           \
  static uint8_t                                                                           \
      scalar_decomposition_length_utf16##endianness##_##decomp_form##_bmp(                 \
          uint16_t code_point) {                                                           \
    uint16_t shift = code_point >> 6;                                                      \
    uint16_t masked = code_point & 63;                                                     \
    uint16_t index =                                                                       \
        NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_INDEX[shift];                     \
    uint8_t value =                                                                        \
        NORMDATA_UTF16_##decomp_form_upper##_LENGTH_TRIE_DATA[index + masked];             \
    return value;                                                                          \
  }                                                                                        \
                                                                                           \
  size_t scalar_normalize_utf16##endianness##_##decomp_form##_length(                      \
      const uint8_t *input, size_t length) {                                               \
    size_t out_length = 0;                                                                 \
    size_t p = 0;                                                                          \
    while (p < length) {                                                                   \
      uint8_t size;                                                                        \
      uint32_t code_point =                                                                \
          scalar_parse_code_point_utf16##endianness(input + p, &size);                     \
      if (size == 2) {                                                                     \
        out_length +=                                                                      \
            scalar_decomposition_length_utf16##endianness##_##decomp_form##_bmp(           \
                code_point);                                                               \
      } else {                                                                             \
        assert(size == 4);                                                                 \
        out_length +=                                                                      \
            scalar_decomposition_length_utf16##endianness##_##decomp_form##_supplementary( \
                code_point);                                                               \
      }                                                                                    \
      p += size;                                                                           \
    }                                                                                      \
    return out_length;                                                                     \
  }

SCALAR_UTF16_IMPLEMENTATION(le, false, nfd, NFD, nfc, NFC);
SCALAR_UTF16_IMPLEMENTATION(be, true, nfd, NFD, nfc, NFC);
SCALAR_UTF16_IMPLEMENTATION(le, false, nfkd, NFKD, nfkc, NFKC);
SCALAR_UTF16_IMPLEMENTATION(be, true, nfkd, NFKD, nfkc, NFKC);

#undef SCALAR_UTF16_IMPLEMENTATION
