#ifndef UTF8NORM_SCALAR_COMMON_H
#define UTF8NORM_SCALAR_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void scalar_sort_characters(uint8_t *out);
uint32_t scalar_phash(uint32_t key, uint32_t salt, uint64_t size);
size_t scalar_write_code_point(uint32_t codepoint, uint8_t *utf8_bytes);
size_t scalar_code_point_size(uint32_t codepoint);
bool scalar_is_hangul(uint32_t code_point);
size_t scalar_decompose_hangul(uint32_t code_point, uint8_t *out);
uint8_t scalar_lookup_ccc(uint32_t code_point);
uint32_t scalar_parse_code_point(uint8_t const *input, uint8_t *size);
bool scalar_is_leading_utf8_byte(uint8_t b);

#endif // UTF8NORM_SCALAR_COMMON_H
