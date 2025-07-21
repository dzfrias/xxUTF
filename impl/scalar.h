#ifndef UTF8NORM_SCALAR_H
#define UTF8NORM_SCALAR_H

#include <stddef.h>

size_t normalize_utf8_nfd_scalar(char const *input, size_t length, char *out);

#endif // UTF8NORM_SCALAR_H
