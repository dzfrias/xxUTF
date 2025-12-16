#ifndef UTF8NORM_IMPLEMENTATION_NEON
#if defined(__aarch64__) || defined(_M_ARM64)
#define UTF8NORM_IMPLEMENTATION_NEON 1
#else
#define UTF8NORM_IMPLEMENTATION_NEON 0
#endif
#endif

#ifndef UTF8NORM_BIG_ENDIAN
#if defined(__BYTE_ORDER__)
#define UTF8NORM_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(_MSC_VER) || defined(_WIN32)
#define UTF8NORM_BIG_ENDIAN 0
#else
#error                                                                         \
    "Cannot detect endianness. Define UTF8NORM_BIG_ENDIAN via compiler flags."
#endif
#endif

#include "utf8norm.h"

#include "impl/scalar.h"
#if UTF8NORM_IMPLEMENTATION_NEON
#include "impl/neon.h"
#endif

size_t utf8norm_normalize_utf8_nfd(char const *input, size_t length,
                                   char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfd((uint8_t const *)input, length,
                                 (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfd((uint8_t const *)input, length,
                                   (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf8_nfkd(char const *input, size_t length,
                                    char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkd((uint8_t const *)input, length,
                                  (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkd((uint8_t const *)input, length,
                                    (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf8_nfc(char const *input, size_t length,
                                   char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfc((uint8_t const *)input, length,
                                 (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfc((uint8_t const *)input, length,
                                   (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf8_nfkc(char const *input, size_t length,
                                    char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkc((uint8_t const *)input, length,
                                  (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkc((uint8_t const *)input, length,
                                    (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf16le_nfd(char const *input, size_t length,
                                      char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfd((uint8_t const *)input, length,
                                    (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfd((uint8_t const *)input, length,
                                      (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf16be_nfd(char const *input, size_t length,
                                      char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfd((uint8_t const *)input, length,
                                    (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfd((uint8_t const *)input, length,
                                      (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf16le_nfkd(char const *input, size_t length,
                                       char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfkd((uint8_t const *)input, length,
                                     (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfkd((uint8_t const *)input, length,
                                       (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf16be_nfkd(char const *input, size_t length,
                                       char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfkd((uint8_t const *)input, length,
                                     (uint8_t *)out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfkd((uint8_t const *)input, length,
                                       (uint8_t *)out);
#endif
}

size_t utf8norm_normalize_utf16le_nfc(char const *input, size_t length,
                                      char *out) {
  return scalar_normalize_utf16le_nfc((uint8_t const *)input, length,
                                      (uint8_t *)out);
}

size_t utf8norm_normalize_utf16be_nfc(char const *input, size_t length,
                                      char *out) {
  return scalar_normalize_utf16be_nfc((uint8_t const *)input, length,
                                      (uint8_t *)out);
}

size_t utf8norm_normalize_utf16le_nfkc(char const *input, size_t length,
                                       char *out) {
  return scalar_normalize_utf16le_nfkc((uint8_t const *)input, length,
                                       (uint8_t *)out);
}

size_t utf8norm_normalize_utf16be_nfkc(char const *input, size_t length,
                                       char *out) {
  return scalar_normalize_utf16be_nfkc((uint8_t const *)input, length,
                                       (uint8_t *)out);
}
