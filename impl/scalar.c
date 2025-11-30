#include "impl/scalar.h"
#include "impl/scalar_common.h"
#include "normdata.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

size_t scalar_copy_code_points(const uint8_t *input, uint8_t *out, size_t amt) {
  uint8_t *start = out;
  for (size_t i = 0; i < amt; i++) {
    uint8_t size = NORMDATA_UTF8_SIZE[input[0]];
    for (uint8_t j = 0; j < size; j++) {
      *out++ = input[j];
    }
    input += size;
  }
  return out - start;
}

void scalar_print_code_points(const uint8_t *input, size_t length) {
  size_t p = 0;
  while (p < length) {
    uint8_t size;
    uint32_t c = scalar_parse_code_point(input + p, &size);
    printf("%u(p=%zu) ", c, p);
    p += size;
  }
  printf("\n");
}

#define DECOMP_SUFFIX nfd
#define DECOMP_TABLE_NAME NFD
#define COMP_SUFFIX nfc
#define COMP_TABLE_NAME NFC
#include "impl/scalar_impl.c" // amalgamate no_include
#undef DECOMP_SUFFIX
#undef DECOMP_TABLE_NAME
#undef COMP_SUFFIX
#undef COMP_TABLE_NAME

#define DECOMP_SUFFIX nfkd
#define DECOMP_TABLE_NAME NFKD
#define COMP_SUFFIX nfkc
#define COMP_TABLE_NAME NFKC
#include "impl/scalar_impl.c" // amalgamate no_include
#undef DECOMP_SUFFIX
#undef DECOMP_TABLE_NAME
#undef COMP_SUFFIX
#undef COMP_TABLE_NAME
