#ifndef NEON_UTF8_H
#define NEON_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t neon_normalize_utf8_nfd(const uint8_t *input, size_t length,
                               uint8_t *out);
size_t neon_normalize_utf8_nfc(const uint8_t *input, size_t length,
                               uint8_t *out);
size_t neon_normalize_utf8_nfkd(const uint8_t *input, size_t length,
                                uint8_t *out);
size_t neon_normalize_utf8_nfkc(const uint8_t *input, size_t length,
                                uint8_t *out);

#endif // NEON_UTF8_H
