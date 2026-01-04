#ifndef XXUTF_H
#define XXUTF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

size_t xxutf_normalize_utf8_nfd(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf8_nfkd(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf8_nfc(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf8_nfkc(const char *input, size_t length, char *out);

size_t xxutf_normalize_utf16le_nfd(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf16be_nfd(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf16le_nfkd(const char *input, size_t length,
                                    char *out);
size_t xxutf_normalize_utf16be_nfkd(const char *input, size_t length,
                                    char *out);
size_t xxutf_normalize_utf16le_nfc(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf16be_nfc(const char *input, size_t length, char *out);
size_t xxutf_normalize_utf16le_nfkc(const char *input, size_t length,
                                    char *out);
size_t xxutf_normalize_utf16be_nfkc(const char *input, size_t length,
                                    char *out);

size_t xxutf_casefold_utf8(const char *input, size_t length, char *out);
size_t xxutf_casefold_utf16le(const char *input, size_t length, char *out);
size_t xxutf_casefold_utf16be(const char *input, size_t length, char *out);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // XXUTF_H
