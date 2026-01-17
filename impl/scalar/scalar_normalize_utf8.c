#include "impl/scalar.h"
#include "impl/scalar/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Hangul code points can be decomposed into Hangul syllables algorithmically.
static size_t scalar_decompose_hangul_utf8(uint32_t code_point, uint8_t *out) {
  uint32_t s_index = code_point - NORMDATA_S_BASE;
  uint32_t l_index = s_index / NORMDATA_N_COUNT;
  uint32_t v_index = (s_index % NORMDATA_N_COUNT) / NORMDATA_T_COUNT;
  uint32_t t_index = s_index % NORMDATA_T_COUNT;

  size_t nwritten = 0;
  nwritten += scalar_write_code_point_utf8(NORMDATA_L_BASE + l_index, out);
  nwritten +=
      scalar_write_code_point_utf8(NORMDATA_V_BASE + v_index, out + nwritten);
  if (t_index > 0) {
    nwritten +=
        scalar_write_code_point_utf8(NORMDATA_T_BASE + t_index, out + nwritten);
  }
  return nwritten;
}

// Sort combining characters in-place (implementation of the canonical ordering
// algorithm). This is done by walking backwards from the end of the buffer
// until a starter character is found and sorting the combining characters from
// there.
//
// Returns the character class of the last character in the sorting range after
// the sort has completed.
uint8_t scalar_sort_characters_utf8(uint8_t *out, size_t length) {
  if (length == 0) {
    return 0;
  }

  uint8_t *start = out;
  // Tracks the ccc of the final character in the sorting range.
  uint8_t final_ccc = 255;

  // We need to walk backwards until we find a starter character.
  uint8_t last_ccc = 255;
  bool needs_sort = false;
  out--;
  while ((size_t)(start - out) <= length) {
    while (!scalar_is_leading_utf8_byte(*out)) {
      out--;
    }
    uint8_t size;
    uint32_t code_point = scalar_parse_code_point_utf8(out, &size);
    uint8_t ccc = scalar_lookup_ccc(code_point);
    if (final_ccc == 255) {
      final_ccc = ccc;
    }
    if (last_ccc < ccc) {
      needs_sort = true;
    }
    // If we found a starter, then we're done
    if (ccc == 0) {
      break;
    }
    out--;
    last_ccc = ccc;
  }

  // Fast path if the combining characters are already sorted
  if (!needs_sort) {
    return final_ccc;
  }

  // We do bubble sort on starting at the starter code point, up until the next
  // starter. The implementation supports sorting any number of combining
  // characters with no memory allocation. Sorting is thus done entirely
  // in-place and still while all code points are in UTF-8-encoded form. In
  // practice, n will be small.
  size_t n = start - out;
  // This loop will run until we detect no more swaps, in which case we will
  // have sorted the buffer.
  while (true) {
    bool did_swap = false;
    uint8_t last_size;
    for (size_t j = 0; j < n; j += last_size) {
      uint8_t size1;
      uint8_t size2;
      // TODO: odds are we can fit this information into a small cache from the
      //       initial backwards loop
      uint32_t c1 = scalar_parse_code_point_utf8(out + j, &size1);
      // Going past the buffer is also a stop condition
      if (j + size1 >= n) {
        break;
      }
      uint32_t c2 = scalar_parse_code_point_utf8(out + j + size1, &size2);
      uint8_t ccc1 = scalar_lookup_ccc(c1);
      uint8_t ccc2 = scalar_lookup_ccc(c2);
      last_size = size1;
      if (ccc1 > ccc2) {
        // Swapping two adjacent, variably sized UTF-8 encoded code points can
        // be done with a right rotation by the size of the right code point.
        scalar_rotate(out + j, size1 + size2, size2);
        last_size = size2;
        did_swap = true;
        if (j + size1 + size2 == n) {
          // Swapped the last character in the sorting range, so update
          // `final_ccc`
          final_ccc = ccc1;
        }
      }
    }
    if (!did_swap) {
      break;
    }
  }

  return final_ccc;
}

// Find a starter character in a UTF-8 buffer, searching from right to left.
size_t scalar_rfind_starter_utf8(const uint8_t *input, size_t length) {
  size_t p = 0;
  while (p < length) {
    while (!scalar_is_leading_utf8_byte(input[length - p - 1])) {
      p++;
    }
    uint8_t size;
    uint32_t c = scalar_parse_code_point_utf8(input + (length - p - 1), &size);
    uint8_t ccc = scalar_lookup_ccc(c);
    // If we found a starter, then we're done
    if (ccc == 0) {
      return length - p - 1;
    }
    p++;
  }
  return -1;
}

#define SCALAR_DEFINE_NORMALIZE_FUNCTIONS(decomp_form, decomp_form_upper,           \
                                          comp_form, comp_form_upper)               \
  /* Decompose a code point and write it into the output buffer. Returns the        \
   * number of bytes written, or zero if the provided code point doesn't have       \
   * a decomposition.                                                               \
   *                                                                                \
   * Note that this does not handle Hangul code points. */                          \
  static size_t scalar_decompose_utf8_##decomp_form##_supplementary(                \
      uint32_t code_point, uint8_t *out, uint8_t *first_ccc, uint8_t *ccc) {        \
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
        out += scalar_write_code_point_utf8(chars[k], out);                         \
      }                                                                             \
      *ccc = kv.last_ccc;                                                           \
    } else {                                                                        \
      *ccc = 0;                                                                     \
    }                                                                               \
    /* `first_ccc` doesn't exist for supplementary code points in the Unicode       \
     * character database. */                                                       \
    *first_ccc = 0;                                                                 \
                                                                                    \
    return out - start;                                                             \
  }                                                                                 \
                                                                                    \
  static size_t scalar_decompose_utf8_##decomp_form##_bmp(                          \
      uint32_t code_point, uint8_t *out, uint8_t *first_ccc, uint8_t *ccc) {        \
    uint8_t *start = out;                                                           \
    uint16_t shift = code_point >> 6;                                               \
    uint16_t masked = code_point & 63;                                              \
    uint16_t index =                                                                \
        NORMDATA_UTF8_##decomp_form_upper##_DATA_TRIE_INDEX[shift];                 \
    uint32_t value =                                                                \
        NORMDATA_UTF8_##decomp_form_upper##_DATA_TRIE_DATA[index + masked];         \
    if (value == 0) {                                                               \
      return 0;                                                                     \
    }                                                                               \
    *ccc = (value >> 21) & 0xFF;                                                    \
    uint16_t offset = value & 0x7FFF;                                               \
    uint8_t length = (value >> 15) & 0x3F;                                          \
    const uint8_t *bytes =                                                          \
        &NORMDATA_UTF8_##decomp_form_upper##_TRIE_DECOMPOSITIONS[offset];           \
    for (size_t k = 0; k < length; k++) {                                           \
      *out++ = bytes[k];                                                            \
    }                                                                               \
    uint8_t ccc_delta = value >> 29;                                                \
    *first_ccc = ccc_delta == 0 ? 0 : *ccc - ccc_delta;                             \
    return out - start;                                                             \
  }                                                                                 \
                                                                                    \
  size_t scalar_find_first_stable_utf8_##decomp_form(const uint8_t *input,          \
                                                     size_t length) {               \
    uint32_t p = 0;                                                                 \
    while (p < length) {                                                            \
      uint8_t size;                                                                 \
      uint32_t c = scalar_parse_code_point_utf8(input + p, &size);                  \
      uint8_t ccc = scalar_lookup_ccc(c);                                           \
      if (ccc == 0 && !scalar_is_##decomp_form##_relevant(c)) {                     \
        return p;                                                                   \
      }                                                                             \
      p += size;                                                                    \
    }                                                                               \
                                                                                    \
    return (size_t)-1;                                                              \
  }                                                                                 \
                                                                                    \
  size_t scalar_find_last_stable_utf8_##decomp_form(const uint8_t *input,           \
                                                    size_t length) {                \
    size_t cutoff = length;                                                         \
    while (cutoff > 0) {                                                            \
      cutoff = scalar_rfind_starter_utf8(input, cutoff);                            \
      if (cutoff == (size_t)-1) {                                                   \
        return (size_t)-1;                                                          \
      }                                                                             \
      uint8_t size;                                                                 \
      uint32_t c = scalar_parse_code_point_utf8(input + cutoff, &size);             \
      uint8_t ccc = scalar_lookup_ccc(c);                                           \
      if (ccc == 0 && !scalar_is_##decomp_form##_relevant(c)) {                     \
        return cutoff;                                                              \
      }                                                                             \
    }                                                                               \
    return (size_t)-1;                                                              \
  }                                                                                 \
                                                                                    \
  size_t scalar_normalize_utf8_##decomp_form##_with_context(                        \
      const uint8_t *input, size_t length, uint8_t *out, size_t out_offset,         \
      uint8_t *last_ccc) {                                                          \
    uint8_t *start = out;                                                           \
                                                                                    \
    size_t p = 0;                                                                   \
    while (p < length) {                                                            \
      uint8_t leading = input[p];                                                   \
                                                                                    \
      uint8_t first_ccc = 0;                                                        \
      uint8_t ccc = 0;                                                              \
      if (leading < 0b10000000) { /* ASCII, no need to do a lookup */               \
        *out++ = leading;                                                           \
        p++;                                                                        \
      } else if ((leading & 0b11100000) == 0b11000000) {                            \
        uint32_t code_point =                                                       \
            (leading & 0b00011111) << 6 | (input[p + 1] & 0b00111111);              \
        size_t nwritten = scalar_decompose_utf8_##decomp_form##_bmp(                \
            code_point, out, &first_ccc, &ccc);                                     \
        if (nwritten == 0) {                                                        \
          *out++ = leading;                                                         \
          *out++ = input[p + 1];                                                    \
        } else {                                                                    \
          out += nwritten;                                                          \
        }                                                                           \
        p += 2;                                                                     \
      } else if ((leading & 0b11110000) == 0b11100000) {                            \
        uint32_t code_point = (leading & 0b00001111) << 12 |                        \
                              (input[p + 1] & 0b00111111) << 6 |                    \
                              (input[p + 2] & 0b00111111);                          \
        if (scalar_is_hangul(code_point)) {                                         \
          out += scalar_decompose_hangul_utf8(code_point, out);                     \
        } else {                                                                    \
          size_t nwritten = scalar_decompose_utf8_##decomp_form##_bmp(              \
              code_point, out, &first_ccc, &ccc);                                   \
          if (nwritten == 0) {                                                      \
            *out++ = leading;                                                       \
            *out++ = input[p + 1];                                                  \
            *out++ = input[p + 2];                                                  \
          } else {                                                                  \
            out += nwritten;                                                        \
          }                                                                         \
        }                                                                           \
        p += 3;                                                                     \
      } else if ((leading & 0b11111000) == 0b11110000) {                            \
        uint32_t code_point =                                                       \
            (leading & 0b00000111) << 18 | (input[p + 1] & 0b00111111) << 12 |      \
            (input[p + 2] & 0b00111111) << 6 | (input[p + 3] & 0b00111111);         \
        size_t nwritten = scalar_decompose_utf8_##decomp_form##_supplementary(      \
            code_point, out, &first_ccc, &ccc);                                     \
        if (nwritten == 0) {                                                        \
          *out++ = leading;                                                         \
          *out++ = input[p + 1];                                                    \
          *out++ = input[p + 2];                                                    \
          *out++ = input[p + 3];                                                    \
        } else {                                                                    \
          out += nwritten;                                                          \
        }                                                                           \
        p += 4;                                                                     \
      }                                                                             \
                                                                                    \
      uint8_t cmp_ccc = first_ccc > 0 ? first_ccc : ccc;                            \
      if (cmp_ccc != 0 && *last_ccc > cmp_ccc) {                                    \
        ccc = scalar_sort_characters_utf8(out, (out - start) + out_offset);         \
      }                                                                             \
      *last_ccc = ccc;                                                              \
    }                                                                               \
                                                                                    \
    return out - start;                                                             \
  }                                                                                 \
                                                                                    \
  size_t scalar_normalize_utf8_##decomp_form(const uint8_t *input,                  \
                                             size_t length, uint8_t *out) {         \
    uint8_t last_ccc = 0;                                                           \
    return scalar_normalize_utf8_##decomp_form##_with_context(                      \
        input, length, out, 0, &last_ccc);                                          \
  }                                                                                 \
                                                                                    \
  size_t scalar_find_first_stable_utf8_##comp_form(const uint8_t *input,            \
                                                   size_t length) {                 \
    uint32_t p = 0;                                                                 \
    while (p < length) {                                                            \
      uint8_t size;                                                                 \
      uint32_t c = scalar_parse_code_point_utf8(input + p, &size);                  \
      uint8_t ccc = scalar_lookup_ccc(c);                                           \
      if (ccc == 0 && !scalar_is_##comp_form##_relevant(c)) {                       \
        return p;                                                                   \
      }                                                                             \
      p += size;                                                                    \
    }                                                                               \
                                                                                    \
    return (size_t)-1;                                                              \
  }                                                                                 \
                                                                                    \
  size_t scalar_find_last_stable_utf8_##comp_form(const uint8_t *input,             \
                                                  size_t length) {                  \
    size_t cutoff = length;                                                         \
    while (cutoff > 0) {                                                            \
      cutoff = scalar_rfind_starter_utf8(input, cutoff);                            \
      if (cutoff == (size_t)-1) {                                                   \
        return (size_t)-1;                                                          \
      }                                                                             \
      uint8_t size;                                                                 \
      uint32_t c = scalar_parse_code_point_utf8(input + cutoff, &size);             \
      uint8_t ccc = scalar_lookup_ccc(c);                                           \
      if (ccc == 0 && !scalar_is_##comp_form##_relevant(c)) {                       \
        return cutoff;                                                              \
      }                                                                             \
    }                                                                               \
    return (size_t)-1;                                                              \
  }                                                                                 \
                                                                                    \
  size_t scalar_normalize_utf8_##comp_form(const uint8_t *input,                    \
                                           size_t length, uint8_t *out) {           \
    uint8_t *start = out;                                                           \
    size_t p = 0;                                                                   \
    uint8_t last_ccc = 0;                                                           \
                                                                                    \
    while (p < length) {                                                            \
      uint8_t size;                                                                 \
      uint32_t c = scalar_parse_code_point_utf8(input + p, &size);                  \
                                                                                    \
      /* ASCII fast path to skip ccc lookup */                                      \
      if (c <= 0x7F) {                                                              \
        *out++ = (uint8_t)c;                                                        \
        p++;                                                                        \
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
      size_t previous_starter_pos = scalar_rfind_starter_utf8(input, p);            \
      if (previous_starter_pos == (size_t)-1) {                                     \
        previous_starter_pos = 0;                                                   \
      }                                                                             \
                                                                                    \
      size_t next_irrelevant_starter_pos =                                          \
          scalar_find_first_stable_utf8_##comp_form(input + p + size,               \
                                                    length - p - size);             \
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
      size_t normalized_length = scalar_normalize_utf8_##decomp_form(               \
          input + previous_starter_pos,                                             \
          next_irrelevant_starter_pos - previous_starter_pos, normalized_out);      \
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
        uint32_t normalized_c = scalar_parse_code_point_utf8(                       \
            normalized_out + normalized_pos, &normalized_size);                     \
        uint8_t normalized_ccc = scalar_lookup_ccc(normalized_c);                   \
                                                                                    \
        /* Find the preceding starter. It should be composition irrelevant */       \
        /* TODO: we can cache this */                                               \
        size_t starter_pos =                                                        \
            scalar_rfind_starter_utf8(normalized_out, normalized_pos);              \
        assert(starter_pos != normalized_pos);                                      \
        /* Skip if we don't have a starter before this */                           \
        if (starter_pos == (size_t)-1) {                                            \
          normalized_pos += normalized_size;                                        \
          normalized_last_ccc = normalized_ccc;                                     \
          continue;                                                                 \
        }                                                                           \
                                                                                    \
        uint8_t starter_size;                                                       \
        uint32_t starter = scalar_parse_code_point_utf8(                            \
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
        uint8_t composed_size = scalar_code_point_size_utf8(composed);              \
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
        (void)scalar_write_code_point_utf8(composed,                                \
                                           normalized_out + starter_pos);           \
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
  }                                                                                 \
                                                                                    \
  static uint8_t scalar_decomposition_length_utf8_##decomp_form##_bmp(              \
      uint16_t code_point) {                                                        \
    uint16_t shift = code_point >> 6;                                               \
    uint16_t masked = code_point & 63;                                              \
    uint16_t index =                                                                \
        NORMDATA_UTF8_##decomp_form_upper##_LENGTH_TRIE_INDEX[shift];               \
    uint8_t value =                                                                 \
        NORMDATA_UTF8_##decomp_form_upper##_LENGTH_TRIE_DATA[index + masked];       \
    return value;                                                                   \
  }                                                                                 \
                                                                                    \
  static uint8_t                                                                    \
      scalar_decomposition_length_utf8_##decomp_form##_supplementary(               \
          uint32_t code_point) {                                                    \
    uint32_t salt_hash = scalar_phash(                                              \
        code_point, 0, NORMDATA_##decomp_form_upper##_TABLE_SIZE);                  \
    uint32_t salt = NORMDATA_##decomp_form_upper##_SALT[salt_hash];                 \
    uint32_t key_hash = scalar_phash(                                               \
        code_point, salt, NORMDATA_##decomp_form_upper##_TABLE_SIZE);               \
    NormdataTableEntry kv = NORMDATA_##decomp_form_upper##_KV[key_hash];            \
    if (kv.k == code_point) {                                                       \
      uint8_t length = 0;                                                           \
      uint32_t const *chars =                                                       \
          &NORMDATA_##decomp_form_upper##_CHARS[kv.offset];                         \
      for (size_t k = 0; k < kv.len; k++) {                                         \
        length += scalar_code_point_size_utf8(chars[k]);                            \
      }                                                                             \
      return length;                                                                \
    }                                                                               \
    return 4;                                                                       \
  }                                                                                 \
                                                                                    \
  static uint8_t scalar_decomposition_length_utf8_##comp_form##_bmp(                \
      uint16_t code_point) {                                                        \
    uint16_t shift = code_point >> 6;                                               \
    uint16_t masked = code_point & 63;                                              \
    uint16_t index =                                                                \
        NORMDATA_UTF8_##comp_form_upper##_LENGTH_TRIE_INDEX[shift];                 \
    uint8_t value =                                                                 \
        NORMDATA_UTF8_##comp_form_upper##_LENGTH_TRIE_DATA[index + masked];         \
    return value;                                                                   \
  }

SCALAR_DEFINE_NORMALIZE_FUNCTIONS(nfd, NFD, nfc, NFC);
SCALAR_DEFINE_NORMALIZE_FUNCTIONS(nfkd, NFKD, nfkc, NFKC);

#undef SCALAR_DEFINE_NORMALIZE_FUNCTIONS

#define SCALAR_NORMALIZE_LENGTH_FUNCTION(form, bmp_function,                   \
                                         supplementary_function)               \
  size_t scalar_normalize_utf8_##form##_length(const uint8_t *input,           \
                                               size_t length) {                \
    size_t out_length = 0;                                                     \
    size_t p = 0;                                                              \
    while (p < length) {                                                       \
      uint8_t leading = input[p];                                              \
      if (leading < 0b10000000) {                                              \
        out_length++;                                                          \
        p++;                                                                   \
      } else if ((leading & 0b11100000) == 0b11000000) {                       \
        uint16_t code_point =                                                  \
            (leading & 0b00011111) << 6 | (input[p + 1] & 0b00111111);         \
        out_length += bmp_function(code_point);                                \
        p += 2;                                                                \
      } else if ((leading & 0b11110000) == 0b11100000) {                       \
        uint16_t code_point = (leading & 0b00001111) << 12 |                   \
                              (input[p + 1] & 0b00111111) << 6 |               \
                              (input[p + 2] & 0b00111111);                     \
        out_length += bmp_function(code_point);                                \
        p += 3;                                                                \
      } else if ((leading & 0b11111000) == 0b11110000) {                       \
        uint32_t code_point =                                                  \
            (leading & 0b00000111) << 18 | (input[p + 1] & 0b00111111) << 12 | \
            (input[p + 2] & 0b00111111) << 6 | (input[p + 3] & 0b00111111);    \
        out_length += supplementary_function(code_point);                      \
        p += 4;                                                                \
      }                                                                        \
    }                                                                          \
    return out_length;                                                         \
  }

SCALAR_NORMALIZE_LENGTH_FUNCTION(
    nfd, scalar_decomposition_length_utf8_nfd_bmp,
    scalar_decomposition_length_utf8_nfd_supplementary);
SCALAR_NORMALIZE_LENGTH_FUNCTION(
    nfkd, scalar_decomposition_length_utf8_nfkd_bmp,
    scalar_decomposition_length_utf8_nfkd_supplementary);
SCALAR_NORMALIZE_LENGTH_FUNCTION(
    nfc, scalar_decomposition_length_utf8_nfc_bmp,
    scalar_decomposition_length_utf8_nfd_supplementary);
SCALAR_NORMALIZE_LENGTH_FUNCTION(
    nfkc, scalar_decomposition_length_utf8_nfkc_bmp,
    scalar_decomposition_length_utf8_nfkd_supplementary);

#undef SCALAR_NORMALIZE_LENGTH_FUNCTION
