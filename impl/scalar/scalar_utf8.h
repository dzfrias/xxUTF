#ifndef XXUTF_SCALAR_UTF8_H
#define XXUTF_SCALAR_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t scalar_normalize_utf8_nfd(const uint8_t *input, size_t length,
                                 uint8_t *out);
size_t scalar_normalize_utf8_nfd_with_context(const uint8_t *input,
                                              size_t length, uint8_t *out,
                                              size_t out_offset,
                                              uint8_t *last_ccc);
size_t scalar_normalize_utf8_nfkd(const uint8_t *input, size_t length,
                                  uint8_t *out);
size_t scalar_normalize_utf8_nfkd_with_context(const uint8_t *input,
                                               size_t length, uint8_t *out,
                                               size_t out_offset,
                                               uint8_t *last_ccc);
size_t scalar_normalize_utf8_nfc(const uint8_t *input, size_t length,
                                 uint8_t *out);
size_t scalar_normalize_utf8_nfkc(const uint8_t *input, size_t length,
                                  uint8_t *out);

// Helpers
size_t scalar_write_code_point_utf8(uint32_t code_point, uint8_t *utf8_bytes);
uint8_t scalar_sort_characters_utf8(uint8_t *out, size_t length);
size_t scalar_sort_utf8_nfd(const uint8_t *input, size_t input_length,
                            uint8_t *out, size_t out_length, size_t *nread);
size_t scalar_sort_utf8_nfkd(const uint8_t *input, size_t input_length,
                             uint8_t *out, size_t out_length, size_t *nread);
size_t scalar_find_nfc_irrelevant_starter_utf8(const uint8_t *input,
                                               size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf8(const uint8_t *input,
                                                size_t length);
size_t scalar_rfind_starter_utf8(const uint8_t *input, size_t length);
void scalar_print_code_points_utf8(const uint8_t *input, size_t length);
size_t scalar_copy_code_points_utf8(const uint8_t *input, uint8_t *out,
                                    size_t amt);

#endif // XXUTF_SCALAR_UTF8_H
