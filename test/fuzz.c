// This file is a fuzz entrypoint for AFL++, using shared memory and persistent
// mode instrumentation. It can also be run without AFL++ instrumentation, in
// which it receives input via stdin.

#include "utf8norm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utf8proc.h>

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

// Normalize a UTF-8 string using utf8proc (NFD form)
static char *utf8proc_normalize_utf8_nfd(const char *input, int32_t len) {
  utf8proc_uint8_t *normalized = NULL;
  utf8proc_ssize_t result =
      utf8proc_map((const utf8proc_uint8_t *)input, len, &normalized,
                   UTF8PROC_DECOMPOSE | UTF8PROC_STABLE);

  if (result < 0) {
    return NULL;
  }

  return (char *)normalized;
}

static char *utf8proc_normalize_utf8_nfc(const char *input, int32_t len) {
  utf8proc_uint8_t *normalized = NULL;
  utf8proc_ssize_t result =
      utf8proc_map((const utf8proc_uint8_t *)input, len, &normalized,
                   UTF8PROC_COMPOSE | UTF8PROC_STABLE);

  if (result < 0) {
    return NULL;
  }

  return (char *)normalized;
}

#ifdef __AFL_FUZZ_TESTCASE_LEN
__AFL_FUZZ_INIT();
#endif

static void print_codepoints(char const *s, ssize_t len) {
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
    uint32_t codepoint;
    unsigned char byte = (unsigned char)s[i];

    if ((byte & 0x80) == 0) {
      codepoint = byte;
      i += 1;
    } else if ((byte & 0xE0) == 0xC0) {
      codepoint = ((byte & 0x1F) << 6) | (s[i + 1] & 0x3F);
      i += 2;
    } else if ((byte & 0xF0) == 0xE0) {
      codepoint =
          ((byte & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
      i += 3;
    } else {
      codepoint = ((byte & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) |
                  ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
      i += 4;
    }

    printf("%04X ", codepoint);
  }
}

int main() {
#ifdef __AFL_FUZZ_TESTCASE_LEN
  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

  while (__AFL_LOOP(1000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;

    // Ensure valid input
    size_t pos;
    if (!is_valid_utf8(buf, len, &pos) || len > 2048) {
      continue;
    }

    char utf8norm_out[16384];
    size_t nwritten =
        utf8norm_normalize_utf8_nfd((char const *)buf, len, utf8norm_out);

    if (!is_valid_utf8((uint8_t const *)utf8norm_out, nwritten, &pos)) {
      abort();
    }
    utf8norm_out[nwritten] = '\0';

    char *utf8proc_out = utf8proc_normalize_utf8_nfd((char const *)buf, len);
    bool eql = equal(utf8norm_out, utf8proc_out);
    free(utf8proc_out);

    if (!eql) {
      abort();
    }

    return 0;
  }
#else
  char buf[4096];
  ssize_t nread;

  while ((nread = read(0, buf, sizeof(buf) - 1)) > 0) {
    buf[nread] = '\0';

    char utf8norm_out_nfd[16384];
    size_t nwritten_nfd =
        utf8norm_normalize_utf8_nfd(buf, nread, utf8norm_out_nfd);
    char utf8norm_out_nfc[16384];
    size_t nwritten_nfc =
        utf8norm_normalize_utf8_nfc(buf, nread, utf8norm_out_nfc);

    size_t pos;
    if (!is_valid_utf8((uint8_t const *)utf8norm_out_nfd, nwritten_nfd, &pos)) {
      printf("normalized (NFD) output is invaild UTF-8, position %zu\n", pos);
      continue;
    }
    utf8norm_out_nfd[nwritten_nfd] = '\0';
    if (!is_valid_utf8((uint8_t const *)utf8norm_out_nfc, nwritten_nfc, &pos)) {
      printf("normalized (NFC) output is invaild UTF-8, position %zu\n", pos);
      continue;
    }
    utf8norm_out_nfc[nwritten_nfc] = '\0';

    char *utf8proc_out_nfd = utf8proc_normalize_utf8_nfd(buf, nread);
    char *utf8proc_out_nfc = utf8proc_normalize_utf8_nfc(buf, nread);

    if (equal(utf8norm_out_nfd, utf8proc_out_nfd)) {
      printf("Both buffers (NFD) equal!\n");
    } else {
      printf("Buffers (NFC) not equal\n");
      printf("   input: ");
      print_codepoints(buf, nread);
      printf("\n");
      printf("utf8norm: ");
      print_codepoints(utf8norm_out_nfd, -1);
      printf("\n");
      printf("utf8proc: ");
      print_codepoints(utf8proc_out_nfd, -1);
      printf("\n");
    }
    if (equal(utf8norm_out_nfc, utf8proc_out_nfc)) {
      printf("Both buffers (NFC) equal!\n");
    } else {
      printf("Buffers (NFC) not equal\n");
      printf("   input: ");
      print_codepoints(buf, nread);
      printf("\n");
      printf("utf8norm: ");
      print_codepoints(utf8norm_out_nfc, -1);
      printf("\n");
      printf("utf8proc: ");
      print_codepoints(utf8proc_out_nfc, -1);
      printf("\n");
    }
    free(utf8proc_out_nfd);
    free(utf8proc_out_nfc);
  }

  return 0;
#endif
}
