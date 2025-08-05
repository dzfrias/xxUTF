#ifndef UTF8NORM_NEON_H
#define UTF8NORM_NEON_H

#include <stddef.h>
#include <stdint.h>

size_t neon_normalize_utf8_nfd(uint8_t const *input, size_t length,
                               uint8_t *out);

#endif // UTF8NORM_NEON_H
