#ifndef UTF8NORM_NEON_H
#define UTF8NORM_NEON_H

#include <stddef.h>
#include <stdint.h>

size_t neon_normalize_utf8_nfd(uint8_t const *input, size_t length,
                               uint8_t *out);
size_t neon_normalize_utf8_nfc(uint8_t const *input, size_t length,
                               uint8_t *out);
size_t neon_normalize_utf8_nfkd(uint8_t const *input, size_t length,
                                uint8_t *out);
size_t neon_normalize_utf8_nfkc(uint8_t const *input, size_t length,
                                uint8_t *out);

size_t neon_normalize_utf16le_nfd(uint8_t const *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16be_nfd(uint8_t const *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16le_nfkd(uint8_t const *input, size_t length,
                                   uint8_t *out);
size_t neon_normalize_utf16be_nfkd(uint8_t const *input, size_t length,
                                   uint8_t *out);
size_t neon_normalize_utf16le_nfc(uint8_t const *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16be_nfc(uint8_t const *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16le_nfkc(uint8_t const *input, size_t length,
                                   uint8_t *out);
size_t neon_normalize_utf16be_nfkc(uint8_t const *input, size_t length,
                                   uint8_t *out);

#endif // UTF8NORM_NEON_H
