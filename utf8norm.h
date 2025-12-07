#ifndef UTF8NORM_H
#define UTF8NORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

size_t utf8norm_normalize_utf8_nfd(char const *input, size_t length, char *out);
size_t utf8norm_normalize_utf8_nfkd(char const *input, size_t length,
                                    char *out);
size_t utf8norm_normalize_utf8_nfc(char const *input, size_t length, char *out);
size_t utf8norm_normalize_utf8_nfkc(char const *input, size_t length,
                                    char *out);

size_t utf8norm_normalize_utf16le_nfd(char const *input, size_t length,
                                      char *out);
size_t utf8norm_normalize_utf16be_nfd(char const *input, size_t length,
                                      char *out);
size_t utf8norm_normalize_utf16le_nfkd(char const *input, size_t length,
                                       char *out);
size_t utf8norm_normalize_utf16be_nfkd(char const *input, size_t length,
                                       char *out);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // UTF8NORM_H
