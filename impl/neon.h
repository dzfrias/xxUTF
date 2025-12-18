#ifndef XXUTF_NEON_H
#define XXUTF_NEON_H

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

size_t neon_normalize_utf16le_nfd(const uint8_t *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16be_nfd(const uint8_t *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16le_nfkd(const uint8_t *input, size_t length,
                                   uint8_t *out);
size_t neon_normalize_utf16be_nfkd(const uint8_t *input, size_t length,
                                   uint8_t *out);
size_t neon_normalize_utf16le_nfc(const uint8_t *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16be_nfc(const uint8_t *input, size_t length,
                                  uint8_t *out);
size_t neon_normalize_utf16le_nfkc(const uint8_t *input, size_t length,
                                   uint8_t *out);
size_t neon_normalize_utf16be_nfkc(const uint8_t *input, size_t length,
                                   uint8_t *out);

#endif // XXUTF_NEON_H
