#ifndef UTF8NORM_SCALAR_H
#define UTF8NORM_SCALAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t scalar_normalize_utf8_nfd(uint8_t const *input, size_t length,
                                 uint8_t *out);
size_t scalar_normalize_utf8_nfd_with_context(uint8_t const *input,
                                              size_t length, uint8_t *out,
                                              bool *end_is_cc);
size_t scalar_normalize_utf8_nfkd(uint8_t const *input, size_t length,
                                  uint8_t *out);
size_t scalar_normalize_utf8_nfkd_with_context(uint8_t const *input,
                                               size_t length, uint8_t *out,
                                               bool *end_is_cc);
size_t scalar_normalize_utf8_nfc(uint8_t const *input, size_t length,
                                 uint8_t *out);
size_t scalar_normalize_utf8_nfkc(uint8_t const *input, size_t length,
                                  uint8_t *out);

// Helper functions
void scalar_sort_characters_utf8(uint8_t *out);
size_t scalar_decompose_utf8_nfd(uint32_t code_point, uint8_t *out,
                                 bool *is_cc);
size_t scalar_decompose_utf8_nfkd(uint32_t code_point, uint8_t *out,
                                  bool *is_cc);
size_t scalar_find_nfc_irrelevant_starter_utf8(uint8_t const *input,
                                               size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf8(uint8_t const *input,
                                                size_t length);
size_t scalar_rfind_starter_utf8(uint8_t const *input, size_t length);
void scalar_print_code_points_utf8(const uint8_t *input, size_t length);
size_t scalar_copy_code_points_utf8(const uint8_t *input, uint8_t *out,
                                    size_t amt);

#endif // UTF8NORM_SCALAR_H
