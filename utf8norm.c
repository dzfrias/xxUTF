#include "utf8norm.h"
#include "impl/neon.h"

size_t utf8norm_normalize_utf8_nfd(char const *input, size_t length,
                                   char *out) {
  return normalize_utf8_nfd_neon(input, length, out);
}
