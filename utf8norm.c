#ifndef UTF8NORM_IMPLEMENTATION_NEON
#if defined(__aarch64__) || defined(_M_ARM64)
#define UTF8NORM_IMPLEMENTATION_NEON 1
#else
#define UTF8NORM_IMPLEMENTATION_NEON 0
#endif
#endif

#include "utf8norm.h"

#if UTF8NORM_IMPLEMENTATION_NEON
#include "impl/neon.h"
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
#include "impl/scalar.h"
#endif

size_t utf8norm_normalize_utf8_nfd(char const *input, size_t length,
                                   char *out) {
#if UTF8NORM_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfd(input, length, out);
#endif
#if !UTF8NORM_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfd(input, length, out);
#endif
}
