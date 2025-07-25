#ifndef UTF8NORM_NEON_H
#define UTF8NORM_NEON_H

#include <stddef.h>

size_t neon_normalize_utf8_nfd(char const *input, size_t length, char *out);

#endif // UTF8NORM_NEON_H
