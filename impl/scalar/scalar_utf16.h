#ifndef UTF8NORM_SCALAR_UTF16_H
#define UTF8NORM_SCALAR_UTF16_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Functions for normalizing a buffer in a certain normalization form.
// The `_with_context` variations give two extra bits of information:
// 1. out_offset: tracks how far the `out` pointer is (implying that) it is a
//                slice of a larger superset buffer.
// 2. end_is_cc: tracks a boolean if the last character processed in the input
//               is a character with a positive Canonical Combining Class.
size_t scalar_normalize_utf16le_nfd(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16le_nfd_with_context(const uint8_t *input,
                                                 size_t length, uint8_t *out,
                                                 size_t out_offset,
                                                 bool *end_is_cc);
size_t scalar_normalize_utf16le_nfkd(const uint8_t *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16le_nfkd_with_context(const uint8_t *input,
                                                  size_t length, uint8_t *out,
                                                  size_t out_offset,
                                                  bool *end_is_cc);
size_t scalar_normalize_utf16be_nfd(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16be_nfd_with_context(const uint8_t *input,
                                                 size_t length, uint8_t *out,
                                                 size_t out_offset,
                                                 bool *end_is_cc);
size_t scalar_normalize_utf16be_nfkd(const uint8_t *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16be_nfkd_with_context(const uint8_t *input,
                                                  size_t length, uint8_t *out,
                                                  size_t out_offset,
                                                  bool *end_is_cc);

size_t scalar_normalize_utf16le_nfc(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16le_nfkc(const uint8_t *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16be_nfc(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16be_nfkc(const uint8_t *input, size_t length,
                                     uint8_t *out);

// Helpers
void scalar_write_uint16_le(uint16_t x, uint8_t *out);
void scalar_write_uint16_be(uint16_t x, uint8_t *out);
void scalar_sort_characters_utf16le(uint8_t *out, size_t length);
void scalar_sort_characters_utf16be(uint8_t *out, size_t length);
size_t scalar_decompose_utf16le_nfd(uint32_t code_point, uint8_t *out,
                                    bool *is_cc);
size_t scalar_decompose_utf16le_nfkd(uint32_t code_point, uint8_t *out,
                                     bool *is_cc);
size_t scalar_decompose_utf16be_nfd(uint32_t code_point, uint8_t *out,
                                    bool *is_cc);
size_t scalar_decompose_utf16be_nfkd(uint32_t code_point, uint8_t *out,
                                     bool *is_cc);
size_t scalar_find_nfc_irrelevant_starter_utf16le(const uint8_t *input,
                                                  size_t length);
size_t scalar_find_nfc_irrelevant_starter_utf16be(const uint8_t *input,
                                                  size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf16le(const uint8_t *input,
                                                   size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf16be(const uint8_t *input,
                                                   size_t length);
size_t scalar_rfind_starter_utf16le(const uint8_t *input, size_t length);
size_t scalar_rfind_starter_utf16be(const uint8_t *input, size_t length);
void scalar_print_code_points_utf16le(const uint8_t *input, size_t length);
void scalar_print_code_points_utf16be(const uint8_t *input, size_t length);

#endif // UTF8NORM_SCALAR_UTF16_H
