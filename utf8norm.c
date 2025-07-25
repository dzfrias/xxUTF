#ifndef UTF8NORM_IMPLEMENTATION_NEON
#define UTF8NORM_IMPLEMENTATION_NEON __aarch64__
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
  return normalize_utf8_nfd_scalar(input, length, out);
#endif
}
