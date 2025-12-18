#ifndef UTF8NORM_H
#define UTF8NORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

size_t utf8norm_normalize_utf8_nfd(const char *input, size_t length, char *out);
size_t utf8norm_normalize_utf8_nfkd(const char *input, size_t length,
                                    char *out);
size_t utf8norm_normalize_utf8_nfc(const char *input, size_t length, char *out);
size_t utf8norm_normalize_utf8_nfkc(const char *input, size_t length,
                                    char *out);

size_t utf8norm_normalize_utf16le_nfd(const char *input, size_t length,
                                      char *out);
size_t utf8norm_normalize_utf16be_nfd(const char *input, size_t length,
                                      char *out);
size_t utf8norm_normalize_utf16le_nfkd(const char *input, size_t length,
                                       char *out);
size_t utf8norm_normalize_utf16be_nfkd(const char *input, size_t length,
                                       char *out);
size_t utf8norm_normalize_utf16le_nfc(const char *input, size_t length,
                                      char *out);
size_t utf8norm_normalize_utf16be_nfc(const char *input, size_t length,
                                      char *out);
size_t utf8norm_normalize_utf16le_nfkc(const char *input, size_t length,
                                       char *out);
size_t utf8norm_normalize_utf16be_nfkc(const char *input, size_t length,
                                       char *out);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // UTF8NORM_H
