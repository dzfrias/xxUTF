#include "utf8norm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unicode/ucnv.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>

#pragma clang optimize off
#pragma GCC optimize("O0")

static bool is_valid_utf8(const uint8_t *data, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint8_t byte = data[i];
    size_t remaining = len - i;

    if (byte <= 0x7F) {
      // ASCII
      i += 1;
    } else if ((byte & 0xE0) == 0xC0) {
      // 2-byte sequence
      if (remaining < 2 || (data[i + 1] & 0xC0) != 0x80 || byte < 0xC2)
        return false;
      i += 2;
    } else if ((byte & 0xF0) == 0xE0) {
      // 3-byte sequence
      if (remaining < 3)
        return false;
      uint8_t b1 = data[i + 1], b2 = data[i + 2];
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
        return false;
      if (byte == 0xE0 && b1 < 0xA0)
        return false; // Overlong encoding
      if (byte == 0xED && b1 >= 0xA0)
        return false; // Surrogates
      i += 3;
    } else if ((byte & 0xF8) == 0xF0) {
      // 4-byte sequence
      if (remaining < 4)
        return false;
      uint8_t b1 = data[i + 1], b2 = data[i + 2], b3 = data[i + 3];
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80)
        return false;
      if (byte == 0xF0 && b1 < 0x90)
        return false; // Overlong
      if (byte == 0xF4 && b1 > 0x8F)
        return false; // > U+10FFFF
      if (byte > 0xF4)
        return false;
      i += 4;
    } else {
      return false;
    }
  }

  return true;
}

static bool equal(char const *a, char const *b) {
  for (size_t i = 0;; i++) {
    if (a[i] == '\0') {
      return b[i] == '\0';
    }
    if (a[i] != b[i]) {
      return false;
    }
  }

  return true;
}

// Helper function to check ICU error and exit if needed
void check_icu_error(UErrorCode status, const char *context) {
  if (U_FAILURE(status)) {
    fprintf(stderr, "ICU error in %s: %s\n", context, u_errorName(status));
    exit(EXIT_FAILURE);
  }
}

// Normalize a UTF-8 string using ICU (NFC form)
char *icu_normalize_utf8_nfd(const char *input_utf8, int32_t len) {
  UErrorCode status = U_ZERO_ERROR;

  // 1. Get NFC Normalizer
  const UNormalizer2 *normalizer = unorm2_getNFDInstance(&status);
  check_icu_error(status, "unorm2_getNFCInstance");

  // 2. Convert UTF-8 -> UTF-16
  int32_t utf16_len = 0;
  u_strFromUTF8(NULL, 0, &utf16_len, input_utf8, len, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    check_icu_error(status, "u_strFromUTF8 preflight");
  }

  status = U_ZERO_ERROR;
  UChar *utf16 = (UChar *)malloc((utf16_len + 1) * sizeof(UChar));
  if (!utf16) {
    fprintf(stderr, "Memory allocation failed for UTF-16 buffer\n");
    exit(1);
  }

  u_strFromUTF8(utf16, utf16_len + 1, NULL, input_utf8, -1, &status);
  check_icu_error(status, "u_strFromUTF8");

  // 3. Normalize UTF-16 string
  int32_t norm_len =
      unorm2_normalize(normalizer, utf16, utf16_len, NULL, 0, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    free(utf16);
    check_icu_error(status, "unorm2_normalize preflight");
  }

  status = U_ZERO_ERROR;
  UChar *norm_utf16 = (UChar *)malloc((norm_len + 1) * sizeof(UChar));
  if (!norm_utf16) {
    fprintf(stderr, "Memory allocation failed for normalized UTF-16 buffer\n");
    free(utf16);
    exit(1);
  }

  unorm2_normalize(normalizer, utf16, utf16_len, norm_utf16, norm_len + 1,
                   &status);
  free(utf16);
  check_icu_error(status, "unorm2_normalize");

  // 4. Convert normalized UTF-16 -> UTF-8
  int32_t utf8_len = 0;
  u_strToUTF8(NULL, 0, &utf8_len, norm_utf16, norm_len, &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {
    free(norm_utf16);
    check_icu_error(status, "u_strToUTF8 preflight");
  }

  status = U_ZERO_ERROR;
  char *output_utf8 = (char *)malloc(utf8_len + 1);
  if (!output_utf8) {
    fprintf(stderr, "Memory allocation failed for output UTF-8 buffer\n");
    free(norm_utf16);
    exit(1);
  }

  u_strToUTF8(output_utf8, utf8_len + 1, NULL, norm_utf16, norm_len, &status);
  free(norm_utf16);
  check_icu_error(status, "u_strToUTF8");

  return output_utf8; // must be freed by the caller
}

__AFL_FUZZ_INIT();

int main(int argc, char **argv) {
  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

  while (__AFL_LOOP(1000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;

    // Ensure valid input
    if (!is_valid_utf8(buf, len) || len > 2048) {
      continue;
    }

    char utf8norm_out[16384];
    size_t nwritten =
        utf8norm_normalize_utf8_nfd((char const *)buf, len, utf8norm_out);
    utf8norm_out[nwritten] = '\0';

    char *icu_out = icu_normalize_utf8_nfd((char const *)buf, len);
    bool eql = equal(utf8norm_out, icu_out);
    free(icu_out);

    if (!eql) {
      printf("UTF-8 buffers didn't match!\n");
      return 1;
    }
  }

  return 0;
}
