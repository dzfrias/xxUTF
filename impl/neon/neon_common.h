#ifndef XXUTF_NEON_COMMON_H
#define XXUTF_NEON_COMMON_H

#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>

void neon_print_uint8x16_t(const char *name, uint8x16_t vec);
void neon_print_uint8x8_t(const char *name, uint8x8_t vec);
void neon_print_uint16x8_t(const char *name, uint16x8_t vec);
void neon_print_uint16x4_t(const char *name, uint16x4_t vec);
void neon_print_uint32x4_t(const char *name, uint32x4_t vec);
void neon_print_uint32x2_t(const char *name, uint32x2_t vec);

// Check if a code point vector contains Hangul syllables. The result is a
// vector of 0s and 0xFFFFFFFFs, where 0 means the code point is not a Hangul
// syllable and 0xFFFFFFFF means it is a Hangul syllable.
uint32x4_t neon_hangul_mask(uint32x4_t input);

// Compute the L, V, and T indices for Hangul syllable decomposition.
//
// https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401
uint16x4x3_t neon_compute_hangul_jamo(uint16x4_t chars);

// memcpy for inputs less than 64 bytes large. The destination buffer needs at
// lesat 64 bytes of space.
void neon_memcpy_small(uint8_t *dst, const uint8_t *src);

// Return the offset of the first 0xFFFFFFFF value in the given logical vector.
uint8_t neon_first_true(uint32x4_t v);

// Pass a code point vector through the NFD bloom filter, which will return a
// vector of 0s and 0xFFFFFFFFs probabilistically indicating whether the
// corresponding code point has an decomposition or is a combining character.
uint32x4x2_t neon_evaluate_bloom_nfd(uint32x4_t input);
uint32x4x2_t neon_evaluate_bloom_nfkd(uint32x4_t input);

uint32x4_t neon_evaluate_bloom_nfc(uint32x4_t input);
uint32x4_t neon_evaluate_bloom_nfkc(uint32x4_t input);

#endif // XXUTF_NEON_COMMON_H
