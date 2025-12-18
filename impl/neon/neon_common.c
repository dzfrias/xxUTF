// amalgamate add: #if XXUTF_IMPLEMENTATION_NEON

#include "impl/neon/neon_common.h"
#include "normdata.h"
#include <arm_neon.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Macro to define a print function for an arbitrarily-shaped NEON vector.
#define NEON_PRINT_FUNC(type, child_type, store_func)                          \
  __attribute__((unused)) void neon_print_##type(const char *name, type vec) { \
    child_type values[sizeof(type) / sizeof(child_type)];                      \
    store_func(values, vec);                                                   \
    printf("%s: ", name);                                                      \
    for (uint8_t i = 0; i < sizeof(values) / sizeof(child_type); i++) {        \
      printf("%04x ", values[i]);                                              \
    }                                                                          \
    printf("\n");                                                              \
  }

NEON_PRINT_FUNC(uint8x16_t, uint8_t, vst1q_u8);
NEON_PRINT_FUNC(uint8x8_t, uint8_t, vst1_u8);
NEON_PRINT_FUNC(uint16x8_t, uint16_t, vst1q_u16);
NEON_PRINT_FUNC(uint16x4_t, uint16_t, vst1_u16);
NEON_PRINT_FUNC(uint32x4_t, uint32_t, vst1q_u32);
NEON_PRINT_FUNC(uint32x2_t, uint32_t, vst1_u32);

#undef NEON_PRINT_FUNC

uint32x4_t neon_hangul_mask(uint32x4_t input) {
  uint32x4_t ge = vcgeq_u32(input, vdupq_n_u32(NORMDATA_S_BASE));
  uint32x4_t lt =
      vcltq_u32(input, vdupq_n_u32(NORMDATA_S_BASE + NORMDATA_S_COUNT));
  uint32x4_t cmp = vandq_u32(lt, ge);
  return cmp;
}

uint16x4x3_t neon_compute_hangul_jamo(uint16x4_t chars) {
  // Compute the S index
  uint16x4_t s = vsub_u16(chars, vdup_n_u16(NORMDATA_S_BASE));

  uint32x4_t l_fixed = vmull_n_u16(s, 28533);
  // Shift the fixed point number
  uint32x4_t l_wide = vshrq_n_u32(l_fixed, 24);
  // L index: s / N_COUNT
  uint16x4_t l = vmovn_u32(l_wide);

  // Multiply and subtract to get the remainder
  uint16x4_t v_modulo = vmls_n_u16(s, l, NORMDATA_N_COUNT);
  uint32x4_t v_fixed = vmull_n_u16(v_modulo, 2341);
  uint32x4_t v_wide = vshrq_n_u32(v_fixed, 16);
  // V index: (s % N_COUNT) / T_COUNT
  uint16x4_t v = vmovn_u32(v_wide);

  uint16x4_t t_shifted = vshr_n_u16(s, 2);
  uint32x4_t t_fixed = vmull_n_u16(t_shifted, 18725);
  // s / T_COUNT
  uint32x4_t t_div_wide = vshrq_n_u32(t_fixed, 17);
  uint16x4_t t_div = vmovn_u32(t_div_wide);
  // T index: s % T_COUNT
  uint16x4_t t = vmls_n_u16(s, t_div, NORMDATA_T_COUNT);

  uint16x4x3_t vals;
  vals.val[0] = vadd_u16(l, vdup_n_u16(NORMDATA_L_BASE));
  vals.val[1] = vadd_u16(v, vdup_n_u16(NORMDATA_V_BASE));
  vals.val[2] = vadd_u16(t, vdup_n_u16(NORMDATA_T_BASE));

  return vals;
}

void neon_memcpy_small(uint8_t *dst, const uint8_t *src) {
  vst1q_u8(dst, vld1q_u8(src));
  vst1q_u8(dst + 16, vld1q_u8(src + 16));
  vst1q_u8(dst + 32, vld1q_u8(src + 32));
  vst1q_u8(dst + 48, vld1q_u8(src + 48));
}

uint8_t neon_first_true(uint32x4_t v) {
  uint16x8_t v16 = vreinterpretq_u16_u8(v);
  uint8x8_t res = vshrn_n_u16(v16, 4);
  uint64_t bitmask4 = vget_lane_u64(vreinterpret_u64_u8(res), 0);
  return __builtin_ctzll(bitmask4) / 16;
}

// Extremely fast, low quality hash function
static uint32x4_t neon_mul_shift_hash(uint32x4_t x) {
  uint32x4_t mul = vmulq_n_u32(x, 2654435761);
  uint32x4_t shift = vshrq_n_u32(mul, 16);
  uint32x4_t y = vandq_u32(shift, vdupq_n_u32(65535));
  return y;
}

// Moderate quality hash function
static uint32x4_t neon_xorshift_hash(uint32x4_t x) {
  x = veorq_u32(x, vshrq_n_u32(x, 13));
  x = veorq_u32(x, vshlq_n_u32(x, 17));
  x = veorq_u32(x, vshrq_n_u32(x, 5));
  return x;
}

// High quality hash function based on the MurmurHash3 finalizer
static uint32x4_t neon_xorshift_mul_hash(uint32x4_t x) {
  x = vmulq_n_u32(veorq_u32(vshrq_n_u32(x, 16), x), 0x45D9F3B);
  x = vmulq_n_u32(veorq_u32(vshrq_n_u32(x, 16), x), 0x45D9F3B);
  x = veorq_u32(vshrq_n_u32(x, 16), x);
  return x;
}

static uint32x4x2_t neon_comp_hash(uint32x4_t input) {
  uint32x4_t h1 = neon_mul_shift_hash(input);
  uint32x4_t h2 = neon_xorshift_hash(input);
  uint32x4_t h3 =
      neon_xorshift_mul_hash(veorq_u32(input, vdupq_n_u32(0xDEADBEEF)));
  uint32x4_t h4 = neon_xorshift_mul_hash(input);

  const uint32_t BLOOM_SIZE =
      sizeof(NORMDATA_NFC_BLOOM_FILTER) / sizeof(uint32_t);
  // h1 % BLOOM_SIZE
  uint32x4_t block_idx = vandq_u32(h1, vdupq_n_u32(BLOOM_SIZE - 1));
  // h2 % 32
  uint32x4_t shift1 = vandq_u32(h2, vdupq_n_u32(31));
  // h3 % 32
  uint32x4_t shift2 = vandq_u32(h3, vdupq_n_u32(31));
  // h4 % 32
  uint32x4_t shift3 = vandq_u32(h4, vdupq_n_u32(31));

  uint32x4_t mask = vshlq_u32(vdupq_n_u32(1), shift1);
  mask = vorrq_u32(mask, vshlq_u32(vdupq_n_u32(1), shift2));
  mask = vorrq_u32(mask, vshlq_u32(vdupq_n_u32(1), shift3));

  uint32x4x2_t vals;
  vals.val[0] = block_idx;
  vals.val[1] = mask;
  return vals;
}

#define NEON_COMMON_FUNCTIONS(decomp_form, decomp_form_upper, comp_form,       \
                              comp_form_upper)                                 \
  uint32x4_t neon_evaluate_bloom_##decomp_form(uint32x4_t input) {             \
    uint32x4_t h1 = neon_mul_shift_hash(input);                                \
    uint32x4_t h2 = neon_xorshift_hash(input);                                 \
    uint32x4_t h3 = neon_xorshift_mul_hash(input);                             \
                                                                               \
    const uint32_t BLOOM_SIZE =                                                \
        sizeof(NORMDATA_##decomp_form_upper##_BLOOM_FILTER) /                  \
        sizeof(uint32_t);                                                      \
    /* h1 % BLOOM_SIZE */                                                      \
    uint32x4_t block_idx = vandq_u32(h1, vdupq_n_u32(BLOOM_SIZE - 1));         \
    /* h2 % 32 */                                                              \
    uint32x4_t shift1 = vandq_u32(h2, vdupq_n_u32(31));                        \
    /* h3 % 32 */                                                              \
    uint32x4_t shift2 = vandq_u32(h3, vdupq_n_u32(31));                        \
    /* (h2 + h3) % 32 */                                                       \
    uint32x4_t shift3 = vandq_u32(vaddq_u32(h2, h3), vdupq_n_u32(31));         \
                                                                               \
    uint32x4_t mask = vshlq_u32(vdupq_n_u32(1), shift1);                       \
    mask = vorrq_u32(mask, vshlq_u32(vdupq_n_u32(1), shift2));                 \
    mask = vorrq_u32(mask, vshlq_u32(vdupq_n_u32(1), shift3));                 \
                                                                               \
    uint32x4_t block = {                                                       \
        NORMDATA_##decomp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,  \
                                                                   0)],        \
        NORMDATA_##decomp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,  \
                                                                   1)],        \
        NORMDATA_##decomp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,  \
                                                                   2)],        \
        NORMDATA_##decomp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,  \
                                                                   3)],        \
    };                                                                         \
                                                                               \
    uint32x4_t result = vandq_u32(mask, block);                                \
    uint32x4_t result_eq = vceqq_u32(result, mask);                            \
                                                                               \
    return result_eq;                                                          \
  }                                                                            \
                                                                               \
  static uint32x4_t neon_evaluate_bloom_##comp_form##_qc(uint32x4_t block_idx, \
                                                         uint32x4_t mask) {    \
    uint32x4_t block = {                                                       \
        NORMDATA_##comp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,    \
                                                                 0)],          \
        NORMDATA_##comp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,    \
                                                                 1)],          \
        NORMDATA_##comp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,    \
                                                                 2)],          \
        NORMDATA_##comp_form_upper##_BLOOM_FILTER[vgetq_lane_u32(block_idx,    \
                                                                 3)],          \
    };                                                                         \
    uint32x4_t result = vandq_u32(mask, block);                                \
    uint32x4_t result_eq = vceqq_u32(result, mask);                            \
    return result_eq;                                                          \
  }                                                                            \
                                                                               \
  static uint32x4_t neon_evaluate_bloom_non_starters_##comp_form(              \
      uint32x4_t block_idx, uint32x4_t mask) {                                 \
    uint32x4_t block = {                                                       \
        NORMDATA_NON_STARTERS_BLOOM_FILTER[vgetq_lane_u32(block_idx, 0)],      \
        NORMDATA_NON_STARTERS_BLOOM_FILTER[vgetq_lane_u32(block_idx, 1)],      \
        NORMDATA_NON_STARTERS_BLOOM_FILTER[vgetq_lane_u32(block_idx, 2)],      \
        NORMDATA_NON_STARTERS_BLOOM_FILTER[vgetq_lane_u32(block_idx, 3)],      \
    };                                                                         \
    uint32x4_t result = vandq_u32(mask, block);                                \
    uint32x4_t result_eq = vceqq_u32(result, mask);                            \
    return result_eq;                                                          \
  }                                                                            \
                                                                               \
  uint32x4_t neon_evaluate_bloom_##comp_form(uint32x4_t input) {               \
    uint32x4x2_t hash_info = neon_comp_hash(input);                            \
    /* NF(K)C QC and the non starters bloom filter use the same hashing scheme \
     */                                                                        \
    uint32x4_t qc = neon_evaluate_bloom_##comp_form##_qc(hash_info.val[0],     \
                                                         hash_info.val[1]);    \
    uint32x4_t non_starters = neon_evaluate_bloom_non_starters_##comp_form(    \
        hash_info.val[0], hash_info.val[1]);                                   \
    uint32x4_t combined = vorrq_u32(qc, non_starters);                         \
    return combined;                                                           \
  }

NEON_COMMON_FUNCTIONS(nfd, NFD, nfc, NFC);
NEON_COMMON_FUNCTIONS(nfkd, NFKD, nfkc, NFKC);

#undef NEON_COMMON_FUNCTIONS

// amalgamate add: #endif // XXUTF_IMPLEMENTATION_NEON
