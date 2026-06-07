#ifndef XXUTF_NEON_H
#define XXUTF_NEON_H

#include <stdbool.h>
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
size_t neon_casefold_utf8(const uint8_t *input, size_t length, uint8_t *out);
size_t neon_casefold_utf16le(const uint8_t *input, size_t length, uint8_t *out);
size_t neon_casefold_utf16be(const uint8_t *input, size_t length, uint8_t *out);

bool neon_normalize_utf8_nfd_check(const uint8_t *input, size_t length,
                                   size_t *out_length);
bool neon_normalize_utf8_nfkd_check(const uint8_t *input, size_t length,
                                    size_t *out_length);
bool neon_normalize_utf8_nfc_check(const uint8_t *input, size_t length,
                                   size_t *out_length);
bool neon_normalize_utf8_nfkc_check(const uint8_t *input, size_t length,
                                    size_t *out_length);
bool neon_normalize_utf16le_nfd_check(const uint8_t *input, size_t length,
                                      size_t *out_length);
bool neon_normalize_utf16be_nfd_check(const uint8_t *input, size_t length,
                                      size_t *out_length);
bool neon_normalize_utf16le_nfkd_check(const uint8_t *input, size_t length,
                                       size_t *out_length);
bool neon_normalize_utf16be_nfkd_check(const uint8_t *input, size_t length,
                                       size_t *out_length);
bool neon_normalize_utf16le_nfc_check(const uint8_t *input, size_t length,
                                      size_t *out_length);
bool neon_normalize_utf16be_nfc_check(const uint8_t *input, size_t length,
                                      size_t *out_length);
bool neon_normalize_utf16le_nfkc_check(const uint8_t *input, size_t length,
                                       size_t *out_length);
bool neon_normalize_utf16be_nfkc_check(const uint8_t *input, size_t length,
                                       size_t *out_length);
bool neon_casefold_utf8_check(const uint8_t *input, size_t length,
                              size_t *out_length);
bool neon_casefold_utf16le_check(const uint8_t *input, size_t length,
                                 size_t *out_length);
bool neon_casefold_utf16be_check(const uint8_t *input, size_t length,
                                 size_t *out_length);

#endif // XXUTF_NEON_H
