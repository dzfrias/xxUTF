#ifndef XXUTF_IMPLEMENTATION_NEON
#if defined(__aarch64__) || defined(_M_ARM64)
#define XXUTF_IMPLEMENTATION_NEON 1
#else
#define XXUTF_IMPLEMENTATION_NEON 0
#endif
#endif

#ifndef XXUTF_BIG_ENDIAN
#if defined(__BYTE_ORDER__)
#define XXUTF_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(_MSC_VER) || defined(_WIN32)
#define XXUTF_BIG_ENDIAN 0
#else
#error "Cannot detect endianness. Define XXUTF_BIG_ENDIAN via compiler flags."
#endif
#endif

#include "xxutf.h"

#include "impl/scalar.h"
#if XXUTF_IMPLEMENTATION_NEON
#include "impl/neon.h"
#endif

size_t xxutf_normalize_utf8_nfd(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfd((const uint8_t *)input, length,
                                 (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfd((const uint8_t *)input, length,
                                   (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf8_nfkd(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkd((const uint8_t *)input, length,
                                  (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkd((const uint8_t *)input, length,
                                    (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf8_nfc(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfc((const uint8_t *)input, length,
                                 (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfc((const uint8_t *)input, length,
                                   (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf8_nfkc(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkc((const uint8_t *)input, length,
                                  (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkc((const uint8_t *)input, length,
                                    (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16le_nfd(const char *input, size_t length,
                                   char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfd((const uint8_t *)input, length,
                                    (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfd((const uint8_t *)input, length,
                                      (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16be_nfd(const char *input, size_t length,
                                   char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfd((const uint8_t *)input, length,
                                    (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfd((const uint8_t *)input, length,
                                      (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16le_nfkd(const char *input, size_t length,
                                    char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfkd((const uint8_t *)input, length,
                                     (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfkd((const uint8_t *)input, length,
                                       (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16be_nfkd(const char *input, size_t length,
                                    char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfkd((const uint8_t *)input, length,
                                     (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfkd((const uint8_t *)input, length,
                                       (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16le_nfc(const char *input, size_t length,
                                   char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfc((const uint8_t *)input, length,
                                    (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfc((const uint8_t *)input, length,
                                      (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16be_nfc(const char *input, size_t length,
                                   char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfc((const uint8_t *)input, length,
                                    (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfc((const uint8_t *)input, length,
                                      (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16le_nfkc(const char *input, size_t length,
                                    char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfkc((const uint8_t *)input, length,
                                     (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfkc((const uint8_t *)input, length,
                                       (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf16be_nfkc(const char *input, size_t length,
                                    char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfkc((const uint8_t *)input, length,
                                     (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfkc((const uint8_t *)input, length,
                                       (uint8_t *)out);
#endif
}

size_t xxutf_casefold_utf8(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf8((const uint8_t *)input, length, (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf8((const uint8_t *)input, length, (uint8_t *)out);
#endif
}

size_t xxutf_casefold_utf16le(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf16le((const uint8_t *)input, length, (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf16le((const uint8_t *)input, length,
                                 (uint8_t *)out);
#endif
}

size_t xxutf_casefold_utf16be(const char *input, size_t length, char *out) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf16be((const uint8_t *)input, length, (uint8_t *)out);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf16be((const uint8_t *)input, length,
                                 (uint8_t *)out);
#endif
}

size_t xxutf_normalize_utf8_nfd_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfd_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfd_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_normalize_utf8_nfkd_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkd_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkd_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_casefold_utf8_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf8_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf8_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_normalize_utf16le_nfd_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfd_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfd_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_normalize_utf16le_nfkd_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfkd_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfkd_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_casefold_utf16le_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf16le_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf16le_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_normalize_utf16be_nfd_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfd_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfd_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_normalize_utf16be_nfkd_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfkd_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfkd_length((const uint8_t *)input, length);
#endif
}
size_t xxutf_casefold_utf16be_length(const char *input, size_t length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf16be_length((const uint8_t *)input, length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf16be_length((const uint8_t *)input, length);
#endif
}
