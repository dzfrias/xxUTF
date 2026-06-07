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

bool xxutf_normalize_utf8_nfd_check(const char *input, size_t length,
                                    size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfd_check((const uint8_t *)input, length,
                                       out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfd_check((const uint8_t *)input, length,
                                         out_length);
#endif
}
bool xxutf_normalize_utf8_nfkd_check(const char *input, size_t length,
                                     size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkd_check((const uint8_t *)input, length,
                                        out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkd_check((const uint8_t *)input, length,
                                          out_length);
#endif
}
bool xxutf_normalize_utf8_nfc_check(const char *input, size_t length,
                                    size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfc_check((const uint8_t *)input, length,
                                       out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfc_check((const uint8_t *)input, length,
                                         out_length);
#endif
}
bool xxutf_normalize_utf8_nfkc_check(const char *input, size_t length,
                                     size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf8_nfkc_check((const uint8_t *)input, length,
                                        out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf8_nfkc_check((const uint8_t *)input, length,
                                          out_length);
#endif
}
bool xxutf_casefold_utf8_check(const char *input, size_t length,
                               size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf8_check((const uint8_t *)input, length, out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf8_check((const uint8_t *)input, length, out_length);
#endif
}
bool xxutf_normalize_utf16le_nfd_check(const char *input, size_t length,
                                       size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfd_check((const uint8_t *)input, length,
                                          out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfd_check((const uint8_t *)input, length,
                                            out_length);
#endif
}
bool xxutf_normalize_utf16le_nfkd_check(const char *input, size_t length,
                                        size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfkd_check((const uint8_t *)input, length,
                                           out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfkd_check((const uint8_t *)input, length,
                                             out_length);
#endif
}
bool xxutf_normalize_utf16le_nfc_check(const char *input, size_t length,
                                       size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfc_check((const uint8_t *)input, length,
                                          out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfc_check((const uint8_t *)input, length,
                                            out_length);
#endif
}
bool xxutf_normalize_utf16le_nfkc_check(const char *input, size_t length,
                                        size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16le_nfkc_check((const uint8_t *)input, length,
                                           out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16le_nfkc_check((const uint8_t *)input, length,
                                             out_length);
#endif
}
bool xxutf_casefold_utf16le_check(const char *input, size_t length,
                                  size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf16le_check((const uint8_t *)input, length,
                                     out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf16le_check((const uint8_t *)input, length,
                                       out_length);
#endif
}
bool xxutf_normalize_utf16be_nfd_check(const char *input, size_t length,
                                       size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfd_check((const uint8_t *)input, length,
                                          out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfd_check((const uint8_t *)input, length,
                                            out_length);
#endif
}
bool xxutf_normalize_utf16be_nfkd_check(const char *input, size_t length,
                                        size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfkd_check((const uint8_t *)input, length,
                                           out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfkd_check((const uint8_t *)input, length,
                                             out_length);
#endif
}
bool xxutf_normalize_utf16be_nfc_check(const char *input, size_t length,
                                       size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfc_check((const uint8_t *)input, length,
                                          out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfc_check((const uint8_t *)input, length,
                                            out_length);
#endif
}
bool xxutf_normalize_utf16be_nfkc_check(const char *input, size_t length,
                                        size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_normalize_utf16be_nfkc_check((const uint8_t *)input, length,
                                           out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_normalize_utf16be_nfkc_check((const uint8_t *)input, length,
                                             out_length);
#endif
}
bool xxutf_casefold_utf16be_check(const char *input, size_t length,
                                  size_t *out_length) {
#if XXUTF_IMPLEMENTATION_NEON
  return neon_casefold_utf16be_check((const uint8_t *)input, length,
                                     out_length);
#endif
#if !XXUTF_IMPLEMENTATION_NEON
  return scalar_casefold_utf16be_check((const uint8_t *)input, length,
                                       out_length);
#endif
}

size_t xxutf_find_last_stable_utf8_nfd(const char *input, size_t length) {
  return scalar_find_last_stable_utf8_nfd((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf8_nfd(const char *input, size_t length) {
  return scalar_find_first_stable_utf8_nfd((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16le_nfd(const char *input, size_t length) {
  return scalar_find_last_stable_utf16le_nfd((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16le_nfd(const char *input, size_t length) {
  return scalar_find_first_stable_utf16le_nfd((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16be_nfd(const char *input, size_t length) {
  return scalar_find_last_stable_utf16be_nfd((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16be_nfd(const char *input, size_t length) {
  return scalar_find_first_stable_utf16be_nfd((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf8_nfkd(const char *input, size_t length) {
  return scalar_find_last_stable_utf8_nfkd((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf8_nfkd(const char *input, size_t length) {
  return scalar_find_first_stable_utf8_nfkd((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16le_nfkd(const char *input, size_t length) {
  return scalar_find_last_stable_utf16le_nfkd((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16le_nfkd(const char *input, size_t length) {
  return scalar_find_first_stable_utf16le_nfkd((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16be_nfkd(const char *input, size_t length) {
  return scalar_find_last_stable_utf16be_nfkd((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16be_nfkd(const char *input, size_t length) {
  return scalar_find_first_stable_utf16be_nfkd((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf8_nfc(const char *input, size_t length) {
  return scalar_find_last_stable_utf8_nfc((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf8_nfc(const char *input, size_t length) {
  return scalar_find_first_stable_utf8_nfc((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16le_nfc(const char *input, size_t length) {
  return scalar_find_last_stable_utf16le_nfc((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16le_nfc(const char *input, size_t length) {
  return scalar_find_first_stable_utf16le_nfc((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16be_nfc(const char *input, size_t length) {
  return scalar_find_last_stable_utf16be_nfc((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16be_nfc(const char *input, size_t length) {
  return scalar_find_first_stable_utf16be_nfc((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf8_nfkc(const char *input, size_t length) {
  return scalar_find_last_stable_utf8_nfkc((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf8_nfkc(const char *input, size_t length) {
  return scalar_find_first_stable_utf8_nfkc((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16le_nfkc(const char *input, size_t length) {
  return scalar_find_last_stable_utf16le_nfkc((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16le_nfkc(const char *input, size_t length) {
  return scalar_find_first_stable_utf16le_nfkc((const uint8_t *)input, length);
}
size_t xxutf_find_last_stable_utf16be_nfkc(const char *input, size_t length) {
  return scalar_find_last_stable_utf16be_nfkc((const uint8_t *)input, length);
}
size_t xxutf_find_first_stable_utf16be_nfkc(const char *input, size_t length) {
  return scalar_find_first_stable_utf16be_nfkc((const uint8_t *)input, length);
}
