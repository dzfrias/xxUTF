#ifndef UTF8NORM_SCALAR_H
#define UTF8NORM_SCALAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t scalar_normalize_utf8_nfd(uint8_t const *input, size_t length,
                                 uint8_t *out);
size_t scalar_normalize_utf8_nfd_with_context(uint8_t const *input,
                                              size_t length, uint8_t *out,
                                              bool *end_is_cc);
bool scalar_is_hangul(uint32_t code_point);
size_t scalar_decompose_hangul(uint32_t code_point, uint8_t *out);
void scalar_sort_characters(uint8_t *out);
size_t scalar_write_codepoint(uint32_t codepoint, uint8_t *utf8_bytes);
size_t scalar_decompose(uint32_t code_point, uint8_t *out, bool *is_cc);

#endif // UTF8NORM_SCALAR_H
