#ifndef XXUTF_SCALAR_COMMON_H
#define XXUTF_SCALAR_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Hash using the perfect hash function for the given key and salt. See
// gen/gen.py for the reference implementation of the perfect hash function.
uint32_t scalar_phash(uint32_t key, uint32_t salt, uint64_t size);

// Check if a code point is in the Hangul block.
bool scalar_is_hangul(uint32_t code_point);

// Reverse a subsection of an array.
void scalar_reverse(uint8_t *array, size_t start, size_t end);

// Rotate a subsection of an array to the right by k positions.
void scalar_rotate(uint8_t *array, size_t size, size_t k);

// Look up the canonical combining class (CCC) for a code point.
//
// See: https://www.unicode.org/reports/tr44/#Canonical_Combining_Class_Values
uint8_t scalar_lookup_ccc(uint32_t code_point);

// Try to compose two BMP code points into a single code point. Returns the
// composed code point if the composition is valid, or zero if the composition
// is not valid.
uint32_t scalar_try_compose_bmp(uint16_t c1, uint16_t c2);

bool scalar_is_nfc_relevant(uint32_t code_point);

bool scalar_is_nfkc_relevant(uint32_t code_point);

// Shift the bytes in a byte buffer to the right by a certain amount.
void scalar_shift_right(uint8_t *buf, size_t length, size_t amt);

// Shift the bytes in a byte buffer to the left by a certain amount.
void scalar_shift_left(uint8_t *buf, size_t length, size_t amt);

// Write a code point into the output buffer as UTF-8 bytes. Returns the number
// of bytes written.
size_t scalar_write_code_point_utf8(uint32_t code_point, uint8_t *utf8_bytes);
size_t scalar_code_point_size_utf8(uint32_t code_point);
// Parse a UTF-8 code point from the input buffer. The size of the code point is
// written to the `size` pointer.
uint32_t scalar_parse_code_point_utf8(const uint8_t *input, uint8_t *size);
// Check if a given byte is the leading byte of a UTF-8 code point
bool scalar_is_leading_utf8_byte(uint8_t b);
size_t scalar_copy_code_points_utf8(const uint8_t *input, uint8_t *out,
                                    size_t amt);
void scalar_print_code_points_utf8(const uint8_t *input, size_t length);

void scalar_write_uint16le(uint16_t x, uint8_t *out);
void scalar_write_uint16be(uint16_t x, uint8_t *out);
uint16_t scalar_read_uint16le(const uint8_t *input);
uint16_t scalar_read_uint16be(const uint8_t *input);
size_t scalar_code_point_size_utf16(uint32_t code_point);
bool scalar_is_utf16_low_surrogate(uint16_t code_unit);
bool scalar_is_utf16_high_surrogate(uint16_t code_unit);
// Write a code point into the output buffer as UTF-16LE bytes. Returns the
// number of bytes written.
size_t scalar_write_code_point_utf16le(uint32_t code_point,
                                       uint8_t *utf16_bytes);
// Write a code point into the output buffer as UTF-16BE bytes. Returns the
// number of bytes written.
size_t scalar_write_code_point_utf16be(uint32_t code_point,
                                       uint8_t *utf16_bytes);
// Parse a UTF-16LE code point from a byte buffer.
uint32_t scalar_parse_code_point_utf16le(const uint8_t *input, uint8_t *size);
// Parse a UTF-16BE code point from a byte buffer.
uint32_t scalar_parse_code_point_utf16be(const uint8_t *input, uint8_t *size);
// Parse a UTF-16LE encoded code point in reverse.
void scalar_print_code_points_utf16le(const uint8_t *input, size_t length);
// Parse a UTF-16BE encoded code point in reverse.
void scalar_print_code_points_utf16be(const uint8_t *input, size_t length);

#endif // XXUTF_SCALAR_COMMON_H
