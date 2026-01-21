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
void neon_print_int8x16_t(const char *name, int8x16_t vec);
void neon_print_int8x8_t(const char *name, int8x8_t vec);
void neon_print_int16x8_t(const char *name, int16x8_t vec);
void neon_print_int16x4_t(const char *name, int16x4_t vec);
void neon_print_int32x4_t(const char *name, int32x4_t vec);
void neon_print_int32x2_t(const char *name, int32x2_t vec);

// Check if a code point vector contains Hangul syllables. The result is a
// vector of 0s and 0xFFFFFFFFs, where 0 means the code point is not a Hangul
// syllable and 0xFFFFFFFF means it is a Hangul syllable.
uint16x4_t neon_hangul_mask(uint16x4_t input);

// Compute the L, V, and T indices for Hangul syllable decomposition.
//
// https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401
uint16x4x3_t neon_compute_hangul_jamo(uint16x4_t chars);

// memcpy for inputs less than 64 bytes large. The destination buffer needs at
// lesat 64 bytes of space.
void neon_memcpy_small(uint8_t *dst, const uint8_t *src);

// Pass a code point vector through the NFC bloom filter, which will return a
// vector of 0s and 0xFFFFFFFFs probabilistically indicating whether the
// corresponding code point has an decomposition or is a combining character.
uint32x4_t neon_evaluate_bloom_nfc(uint32x4_t input);
uint32x4_t neon_evaluate_bloom_nfkc(uint32x4_t input);

uint16x4_t neon_evaluate_trie_nfc(uint16x4_t input);
uint16x4_t neon_evaluate_trie_nfkc(uint16x4_t input);
uint16x8_t neon_evaluate_trie_compound_nfc(uint16x8_t input);
uint16x8_t neon_evaluate_trie_compound_nfkc(uint16x8_t input);
uint16x8_t neon_evaluate_trie_nfc_wide(uint16x8_t input);
uint16x8_t neon_evaluate_trie_nfkc_wide(uint16x8_t input);

// Parse four three-byte UTF-8 code points into their 16-bit code point values.
uint16x4_t neon_parse_3_byte_utf8(uint8x16_t in);
// Parse four two-byte UTF-8 code points into their 16-bit code point values.
uint16x4_t neon_parse_2_byte_utf8(uint8x16_t in);
// Parse three four-byte UTF-8 code points into the 32-bit code point values.
uint32x4_t neon_parse_4_byte_utf8(uint8x16_t in);
// Parse four code points encoded in UTF-8 into 16-bit code point values.
uint16x4_t neon_parse_4_12_utf8(uint8x16_t in, size_t shufutf8_idx);
// Parse four code points encoded in UTF-8 into 16-bit code point values.
uint16x4_t neon_parse_4_123_utf8(uint8x16_t in, size_t shufutf8_idx);
// Parse three code points encoded in UTF-8 into 32-bit code point values.
uint32x4_t neon_parse_3_1234_utf8(uint8x16_t in, size_t shufutf8_idx);

uint8x16_t neon_get_utf8_code_point_starts(uint8x16_t in);
uint64_t neon_make_utf8_code_point_mask(const uint8_t *input);

// Create a logical vector for high surrogates.
uint16x8_t neon_make_utf16_surrogates_mask(uint16x8_t in);

#endif // XXUTF_NEON_COMMON_H
