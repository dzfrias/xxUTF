#ifndef UTF8NORM_SCALAR_H
#define UTF8NORM_SCALAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t scalar_normalize_utf8_nfd(char const *input, size_t length, char *out);
bool scalar_is_hangul(uint32_t code_point);
size_t scalar_decompose_hangul(uint32_t code_point, char *out);

#endif // UTF8NORM_SCALAR_H
