#ifndef XXUTF_SCALAR_H
#define XXUTF_SCALAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// UTF-8
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
size_t scalar_casefold_utf8(const uint8_t *input, size_t length, uint8_t *out);
size_t scalar_casefold_utf16le(const uint8_t *input, size_t length,
                               uint8_t *out);
size_t scalar_casefold_utf16be(const uint8_t *input, size_t length,
                               uint8_t *out);

// UTF-16
size_t scalar_normalize_utf16le_nfd(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16le_nfd_with_context(const uint8_t *input,
                                                 size_t length, uint8_t *out,
                                                 size_t out_length,
                                                 uint8_t *last_ccc);
size_t scalar_normalize_utf16le_nfkd(const uint8_t *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16le_nfkd_with_context(const uint8_t *input,
                                                  size_t length, uint8_t *out,
                                                  size_t out_length,
                                                  uint8_t *last_ccc);
size_t scalar_normalize_utf16be_nfd(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16be_nfd_with_context(const uint8_t *input,
                                                 size_t length, uint8_t *out,
                                                 size_t out_length,
                                                 uint8_t *last_ccc);
size_t scalar_normalize_utf16be_nfkd(const uint8_t *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16be_nfkd_with_context(const uint8_t *input,
                                                  size_t length, uint8_t *out,
                                                  size_t out_length,
                                                  uint8_t *last_ccc);
size_t scalar_normalize_utf16le_nfc(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16le_nfkc(const uint8_t *input, size_t length,
                                     uint8_t *out);
size_t scalar_normalize_utf16be_nfc(const uint8_t *input, size_t length,
                                    uint8_t *out);
size_t scalar_normalize_utf16be_nfkc(const uint8_t *input, size_t length,
                                     uint8_t *out);

// Helper functions
uint8_t scalar_sort_characters_utf8(uint8_t *out, size_t length);
size_t scalar_write_code_point_utf8(uint32_t code_point, uint8_t *out);
size_t scalar_find_nfc_irrelevant_starter_utf8(const uint8_t *input,
                                               size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf8(const uint8_t *input,
                                                size_t length);
size_t scalar_rfind_starter_utf8(const uint8_t *input, size_t length);
void scalar_print_code_points_utf8(const uint8_t *input, size_t length);

void scalar_write_uint16le(uint16_t x, uint8_t *out);
void scalar_write_uint16be(uint16_t x, uint8_t *out);
uint16_t scalar_read_uint16le(const uint8_t *input);
uint16_t scalar_read_uint16be(const uint8_t *input);
uint8_t scalar_sort_characters_utf16le(uint8_t *out, size_t length);
uint8_t scalar_sort_characters_utf16be(uint8_t *out, size_t length);
size_t scalar_find_nfc_irrelevant_starter_utf16le(const uint8_t *input,
                                                  size_t length);
size_t scalar_find_nfc_irrelevant_starter_utf16be(const uint8_t *input,
                                                  size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf16le(const uint8_t *input,
                                                   size_t length);
size_t scalar_find_nfkc_irrelevant_starter_utf16be(const uint8_t *input,
                                                   size_t length);
size_t scalar_rfind_starter_utf16le(const uint8_t *input, size_t length);
size_t scalar_rfind_starter_utf16be(const uint8_t *input, size_t length);
void scalar_print_code_points_utf16le(const uint8_t *input, size_t length);
void scalar_print_code_points_utf16be(const uint8_t *input, size_t length);

#endif // XXUTF_SCALAR_H
