#ifndef UTF8NORM_IMPLEMENTATION_NEON
#if defined(__aarch64__) || defined(_M_ARM64)
#define UTF8NORM_IMPLEMENTATION_NEON 1
#else
#define UTF8NORM_IMPLEMENTATION_NEON 0
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
