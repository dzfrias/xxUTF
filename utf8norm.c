#include "utf8norm.h"

#include "normdata.c"
#include "impl/neon.c"

size_t utf8norm_normalize_utf8_nfd(char const *input, size_t length, char *out) {
  return normalize_utf8_nfd(input, length, out);
}
