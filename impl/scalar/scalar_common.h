#ifndef SCALAR_COMMON_H
#define SCALAR_COMMON_H

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

#endif // SCALAR_COMMON_H
