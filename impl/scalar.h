#ifndef UTF8NORM_SCALAR_H
#define UTF8NORM_SCALAR_H

#include <stddef.h>

size_t scalar_normalize_utf8_nfd(char const *input, size_t length, char *out);

#endif // UTF8NORM_SCALAR_H
