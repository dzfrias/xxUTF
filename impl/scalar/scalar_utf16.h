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

#endif // UTF8NORM_SCALAR_UTF16_H
