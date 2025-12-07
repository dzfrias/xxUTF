// This file is a fuzz entrypoint for AFL++, using shared memory and persistent
// mode instrumentation. It can also be run without AFL++ instrumentation, in
// which it receives input via stdin.

#include "utf8norm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unicode/ucnv.h>
#include <unicode/unorm.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>
#include <unistd.h>

#if defined(__clang__)
#pragma clang optimize off
#elif defined(__GNUC__)
#pragma GCC optimize("O0")
#endif

static bool is_valid_utf8(const uint8_t *data, size_t len, size_t *pos) {
  size_t i = 0;
  while (i < len) {
    uint8_t byte = data[i];
    size_t remaining = len - i;
    *pos = i;

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

static bool is_valid_utf16le(const uint8_t *buffer, size_t len, size_t *pos) {
  if (len % 2 != 0) {
    *pos = 0;
    return false;
  }
  for (size_t i = 0; i < len; i += 2) {
    uint16_t c = (uint8_t)buffer[i] | ((uint8_t)buffer[i + 1] << 8);
    *pos = i;
    if (c >= 0xD800 && c <= 0xDBFF) {
      if (i + 2 >= len) {
        return false;
      }
      uint16_t next = (uint8_t)buffer[i + 2] | ((uint8_t)buffer[i + 3] << 8);
      if (next < 0xDC00 || next > 0xDFFF) {
        return false;
      }
      i += 2;
    } else if (c >= 0xDC00 && c <= 0xDFFF) {
      return false;
    }
  }
  return true;
}

static bool is_valid_utf16be(const uint8_t *buffer, size_t len, size_t *pos) {
  if (len % 2 != 0) {
    *pos = 0;
    return false;
  }

  for (size_t i = 0; i < len; i += 2) {
    uint16_t c = (uint8_t)buffer[i + 1] | ((uint8_t)buffer[i] << 8);
    *pos = i;
    if (c >= 0xD800 && c <= 0xDBFF) {
      if (i + 2 >= len) {
        return false;
      }
      uint16_t next = (uint8_t)buffer[i + 3] | ((uint8_t)buffer[i + 2] << 8);

      if (next < 0xDC00 || next > 0xDFFF) {
        return false;
      }
      i += 2;
    } else if (c >= 0xDC00 && c <= 0xDFFF) {
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

#ifdef __AFL_FUZZ_TESTCASE_LEN
__AFL_FUZZ_INIT();
#endif

static void print_code_points(char const *s, ssize_t len) {
  if (!s)
    return;

  // If len is -1, calculate length using the NUL terminator
  if (len == -1) {
    len = 0;
    while (s[len] != '\0')
      len++;
  }

  ssize_t i = 0;
  while (i < len) {
    uint32_t code_point;
    unsigned char byte = (unsigned char)s[i];

    if ((byte & 0x80) == 0) {
      code_point = byte;
      i += 1;
    } else if ((byte & 0xE0) == 0xC0) {
      code_point = ((byte & 0x1F) << 6) | (s[i + 1] & 0x3F);
      i += 2;
    } else if ((byte & 0xF0) == 0xE0) {
      code_point =
          ((byte & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
      i += 3;
    } else {
      code_point = ((byte & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) |
                   ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
      i += 4;
    }

    printf("%04X ", code_point);
  }
}

#define COMPARE_NORMALIZE_FUNCTION_UTF8(form, form_upper)                      \
  static bool compare_normalization_utf8_##form(char const *input,             \
                                                size_t length, bool verbose) { \
    UErrorCode status = U_ZERO_ERROR;                                          \
    UChar source[8192];                                                        \
    int32_t source_length;                                                     \
    u_strFromUTF8(source, 8192, &source_length, input, length, &status);       \
    if (U_FAILURE(status)) {                                                   \
      printf("error converting to UTF-16: %s\n", u_errorName(status));         \
      return false;                                                            \
    }                                                                          \
                                                                               \
    const UNormalizer2 *normalizer =                                           \
        unorm2_get##form_upper##Instance(&status);                             \
    if (U_FAILURE(status)) {                                                   \
      printf("error getting normalizer: %s\n", u_errorName(status));           \
      return false;                                                            \
    }                                                                          \
                                                                               \
    UChar result[8192];                                                        \
    int32_t result_length = unorm2_normalize(                                  \
        normalizer, source, source_length, result, 8192, &status);             \
    if (U_FAILURE(status)) {                                                   \
      printf("error normalizing: %s\n", u_errorName(status));                  \
      return false;                                                            \
    }                                                                          \
                                                                               \
    char icu_out[8192];                                                        \
    int32_t icu_out_length;                                                    \
    u_strToUTF8(icu_out, 8192, &icu_out_length, result, result_length,         \
                &status);                                                      \
    if (U_FAILURE(status)) {                                                   \
      printf("error converting to UTF-8: %s\n", u_errorName(status));          \
      return false;                                                            \
    }                                                                          \
                                                                               \
    char utf8norm_out[8192];                                                   \
    size_t utf8norm_out_length =                                               \
        utf8norm_normalize_utf8_##form(input, length, utf8norm_out);           \
    size_t pos;                                                                \
    if (!is_valid_utf8((uint8_t const *)utf8norm_out, utf8norm_out_length,     \
                       &pos)) {                                                \
      if (verbose) {                                                           \
        printf("normalized (%s) output is invaild UTF-8, position %zu\n",      \
               #form_upper, pos);                                              \
      }                                                                        \
      return false;                                                            \
    }                                                                          \
                                                                               \
    icu_out[icu_out_length] = '\0';                                            \
    utf8norm_out[utf8norm_out_length] = '\0';                                  \
                                                                               \
    if (!equal(utf8norm_out, icu_out)) {                                       \
      if (verbose) {                                                           \
        printf("Buffers (UTF-8, %s) not equal\n", #form_upper);                \
        printf("   input: ");                                                  \
        print_code_points(input, length);                                      \
        printf("\n");                                                          \
        printf("utf8norm: ");                                                  \
        print_code_points(utf8norm_out, -1);                                   \
        printf("\n");                                                          \
        printf("   icu4c: ");                                                  \
        print_code_points(icu_out, -1);                                        \
        printf("\n");                                                          \
      }                                                                        \
      return false;                                                            \
    }                                                                          \
    if (verbose) {                                                             \
      printf("Both buffers (UTF-8, %s) equal!\n", #form_upper);                \
    }                                                                          \
    return true;                                                               \
  }

COMPARE_NORMALIZE_FUNCTION_UTF8(nfd, NFD);
COMPARE_NORMALIZE_FUNCTION_UTF8(nfc, NFC);
COMPARE_NORMALIZE_FUNCTION_UTF8(nfkd, NFKD);
COMPARE_NORMALIZE_FUNCTION_UTF8(nfkc, NFKC);

#undef COMPARE_NORMALIZE_FUNCTION_UTF8

#define COMPARE_NORMALIZE_FUNCTION_UTF16(form, form_upper, endianness,         \
                                         endianness_upper)                     \
  static bool compare_normalization_utf16##endianness##_##form(                \
      char const *input, size_t length, bool verbose) {                        \
    UErrorCode status = U_ZERO_ERROR;                                          \
    UChar source[8192];                                                        \
    int32_t source_length;                                                     \
    u_strFromUTF8(source, 8192, &source_length, input, length, &status);       \
    if (U_FAILURE(status)) {                                                   \
      printf("error converting to UTF-16: %s\n", u_errorName(status));         \
      return false;                                                            \
    }                                                                          \
                                                                               \
    const UNormalizer2 *normalizer =                                           \
        unorm2_get##form_upper##Instance(&status);                             \
    if (U_FAILURE(status)) {                                                   \
      printf("error getting normalizer: %s\n", u_errorName(status));           \
      return false;                                                            \
    }                                                                          \
                                                                               \
    UChar result[8192];                                                        \
    int32_t result_length = unorm2_normalize(                                  \
        normalizer, source, source_length, result, 8192, &status);             \
    if (U_FAILURE(status)) {                                                   \
      printf("error normalizing: %s\n", u_errorName(status));                  \
      return false;                                                            \
    }                                                                          \
                                                                               \
    UConverter *conv = ucnv_open("UTF-16" #endianness_upper, &status);         \
    if (U_FAILURE(status)) {                                                   \
      printf("error opening UTF-16 converter: %s\n", u_errorName(status));     \
      return false;                                                            \
    }                                                                          \
                                                                               \
    int32_t icu_out_length =                                                   \
        ucnv_fromUChars(conv, NULL, 0, result, result_length, &status);        \
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status)) {              \
      printf("error calculating size: %s\n", u_errorName(status));             \
      ucnv_close(conv);                                                        \
      return 1;                                                                \
    }                                                                          \
    status = U_ZERO_ERROR;                                                     \
                                                                               \
    char icu_out[8192];                                                        \
    ucnv_fromUChars(conv, icu_out, 8192, result, result_length, &status);      \
    if (U_FAILURE(status)) {                                                   \
      printf("error converting to UTF-16: %s\n", u_errorName(status));         \
      ucnv_close(conv);                                                        \
      return false;                                                            \
    }                                                                          \
                                                                               \
    char utf16_bytes[8192];                                                    \
    int32_t utf16_length = ucnv_fromUChars(conv, utf16_bytes, 8192, source,    \
                                           source_length, &status);            \
    if (U_FAILURE(status)) {                                                   \
      printf("error converting to UTF-16: %s\n", u_errorName(status));         \
      ucnv_close(conv);                                                        \
      return false;                                                            \
    }                                                                          \
                                                                               \
    char utf8norm_out[8192];                                                   \
    size_t utf8norm_out_length =                                               \
        utf8norm_normalize_utf16##endianness##_##form(                         \
            utf16_bytes, utf16_length, utf8norm_out);                          \
    size_t pos;                                                                \
    if (!is_valid_utf16##endianness((uint8_t const *)utf8norm_out,             \
                                    utf8norm_out_length, &pos)) {              \
      if (verbose) {                                                           \
        printf("normalized (%s, %s) output is invaild UTF-16, position %zu\n", \
               "UTF-16" #endianness_upper, #form_upper, pos);                  \
      }                                                                        \
      return false;                                                            \
    }                                                                          \
                                                                               \
    icu_out[icu_out_length] = '\0';                                            \
    utf8norm_out[utf8norm_out_length] = '\0';                                  \
                                                                               \
    if (!equal(utf8norm_out, icu_out)) {                                       \
      if (verbose) {                                                           \
        printf("Buffers (%s, %s) not equal\n", "UTF-16" #endianness_upper,     \
               #form_upper);                                                   \
        printf("   input: ");                                                  \
        print_code_points(input, length);                                      \
        printf("\n");                                                          \
        printf("utf8norm: ");                                                  \
        print_code_points(utf8norm_out, -1);                                   \
        printf("\n");                                                          \
        printf("   icu4c: ");                                                  \
        print_code_points(icu_out, -1);                                        \
        printf("\n");                                                          \
      }                                                                        \
      ucnv_close(conv);                                                        \
      return false;                                                            \
    }                                                                          \
    if (verbose) {                                                             \
      printf("Both buffers (%s, %s) equal!\n", "UTF-16" #endianness_upper,     \
             #form_upper);                                                     \
    }                                                                          \
    ucnv_close(conv);                                                          \
    return true;                                                               \
  }

COMPARE_NORMALIZE_FUNCTION_UTF16(nfd, NFD, le, LE);
COMPARE_NORMALIZE_FUNCTION_UTF16(nfd, NFD, be, BE);
COMPARE_NORMALIZE_FUNCTION_UTF16(nfkd, NFKD, le, LE);
COMPARE_NORMALIZE_FUNCTION_UTF16(nfkd, NFKD, be, BE);

#undef COMPARE_NORMALIZE_FUNCTION_UTF16

int main() {
#ifdef __AFL_FUZZ_TESTCASE_LEN
  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

  const char *normalization_form = getenv("UTF8NORM_FUZZ_NORMALIZATION_FORM");
  const char *encoding = getenv("UTF8NORM_FUZZ_ENCODING");
  if (normalization_form == NULL) {
    normalization_form = "NFD";
  }
  if (encoding == NULL) {
    encoding = "UTF-8";
  }

  bool (*validation_func)(uint8_t const *, size_t, size_t *);
  bool (*compare_func)(char const *, size_t, bool);

  if (strcmp(encoding, "UTF-8") == 0) {
    validation_func = is_valid_utf8;
    if (strcmp(normalization_form, "NFD") == 0) {
      compare_func = compare_normalization_utf8_nfd;
    } else if (strcmp(normalization_form, "NFC") == 0) {
      compare_func = compare_normalization_utf8_nfc;
    } else if (strcmp(normalization_form, "NFKD") == 0) {
      compare_func = compare_normalization_utf8_nfkd;
    } else if (strcmp(normalization_form, "NFKC") == 0) {
      compare_func = compare_normalization_utf8_nfkc;
    } else {
      printf("Invalid normalization form: %s\n", normalization_form);
      abort();
    }
  } else if (strcmp(encoding, "UTF-16LE") == 0) {
    validation_func = is_valid_utf16le;
    if (strcmp(normalization_form, "NFD")) {
      compare_func = compare_normalization_utf16le_nfd;
    } else if (strcmp(normalization_form, "NFKD")) {
      compare_func = compare_normalization_utf16le_nfkd;
    } else {
      printf("Invalid normalization form: %s\n", normalization_form);
      abort();
    }
  } else if (strcmp(encoding, "UTF-16BE") == 0) {
    validation_func = is_valid_utf16be;
    if (strcmp(normalization_form, "NFD")) {
      compare_func = compare_normalization_utf16be_nfd;
    } else if (strcmp(normalization_form, "NFKD")) {
      compare_func = compare_normalization_utf16be_nfkd;
    } else {
      printf("Invalid normalization form: %s\n", normalization_form);
      abort();
    }
  } else {
    printf("Invalid encoding form: %s\n", encoding);
    abort();
  }

  while (__AFL_LOOP(1000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;

    // Ensure valid input
    size_t pos;
    if (!validation_func(buf, len, &pos) || len > 2048) {
      continue;
    }

    if (!compare_func((char const *)buf, len, false)) {
      abort();
    }

    return 0;
  }
#else
  char buf[4096];
  ssize_t nread;

  while ((nread = read(0, buf, sizeof(buf) - 1)) > 0) {
    buf[nread] = '\0';

    if (!compare_normalization_utf8_nfd(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf8_nfc(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf8_nfkd(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf8_nfkc(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf16le_nfd(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf16be_nfd(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf16le_nfkd(buf, nread, true)) {
      continue;
    }
    if (!compare_normalization_utf16be_nfkd(buf, nread, true)) {
      continue;
    }
  }

  return 0;
#endif
}
