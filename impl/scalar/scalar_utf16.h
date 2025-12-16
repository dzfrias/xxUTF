#ifndef UTF8NORM_SCALAR_UTF16_H
#define UTF8NORM_SCALAR_UTF16_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t scalar_normalize_utf16le_nfd(uint8_t const *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16le_nfd_with_context(uint8_t const *input,
                                                 size_t length, uint8_t *out,
                                                 bool *end_is_cc);
size_t scalar_normalize_utf16le_nfkd(uint8_t const *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16le_nfkd_with_context(uint8_t const *input,
                                                  size_t length, uint8_t *out,
                                                  bool *end_is_cc);
size_t scalar_normalize_utf16be_nfd(uint8_t const *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16be_nfd_with_context(uint8_t const *input,
                                                 size_t length, uint8_t *out,
                                                 bool *end_is_cc);
size_t scalar_normalize_utf16be_nfkd(uint8_t const *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16be_nfkd_with_context(uint8_t const *input,
                                                  size_t length, uint8_t *out,
                                                  bool *end_is_cc);

size_t scalar_normalize_utf16le_nfc(uint8_t const *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16le_nfkc(uint8_t const *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16be_nfc(uint8_t const *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16be_nfkc(uint8_t const *input, size_t length,
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

#endif // UTF8NORM_SCALAR_UTF16_H
