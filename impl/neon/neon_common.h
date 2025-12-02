#ifndef UTF8NORM_NEON_COMMON_H
#define UTF8NORM_NEON_COMMON_H

#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint8_t neon_movemask_u16(uint16x4_t v);
void neon_print_uint8x16_t(const char *name, uint8x16_t vec);
void neon_print_uint8x8_t(const char *name, uint8x8_t vec);
void neon_print_uint16x8_t(const char *name, uint16x8_t vec);
void neon_print_uint16x4_t(const char *name, uint16x4_t vec);
void neon_print_uint32x4_t(const char *name, uint32x4_t vec);
void neon_print_uint32x2_t(const char *name, uint32x2_t vec);
uint16x4_t neon_parse_3_byte_utf8(uint8x16_t in);
uint16x4_t neon_parse_2_byte_utf8(uint8x16_t in);
uint32x4_t neon_parse_4_byte_utf8(uint8x16_t in);
uint16x4_t neon_parse_4_12_utf8(uint8x16_t in, size_t shufutf8_idx);
uint16x4_t neon_parse_4_123_utf8(uint8x16_t in, size_t shufutf8_idx);
uint32x4_t neon_parse_3_1234_utf8(uint8x16_t in, size_t shufutf8_idx);
void neon_write_8_3_byte_utf8(uint16x8_t in, uint8_t *out);
uint32x4_t neon_mul_shift_hash(uint32x4_t x);
uint32x4_t neon_xorshift_hash(uint32x4_t x);
uint32x4_t neon_xorshift_mul_hash(uint32x4_t x);
uint32x4_t neon_hangul_mask(uint32x4_t input);
void neon_decompose_hangul(uint32x4_t values, uint32x4_t relevant,
                           uint8_t **out, const uint8_t *input,
                           bool *end_is_cc);
void neon_decompose_all_hangul(uint16x4_t values, uint8_t **out,
                               bool *end_is_cc);
void neon_skip_decomp(uint8x16_t in, size_t nchars, uint8_t **out,
                      bool *end_is_cc);
void neon_memcpy_small(uint8_t *dst, const uint8_t *src, size_t len);
uint32x4x2_t neon_comp_hash(uint32x4_t input);
uint8_t neon_first_true(uint32x4_t v);
uint8x16_t neon_get_codepoint_starts(uint8x16_t in);
uint64_t neon_make_code_point_mask(uint8_t const *input);

#endif // UTF8NORM_NEON_COMMON_H
