#include <arm_neon.h>
#include <stdio.h>

// Compute a 64-bit mask corresponding to the start byte of each code point in
// the given input vector.
//
// It can be thought of as a 16-bit mask, but with four bits per single bit in
// a 16-bit mask. Example:
// 1001000000000001 => 1111000000001111000000000000000000000000000000000000000000001111
static inline uint64x1_t compute_maskq_u16(uint8x16_t input) {
  int8x16_t sgn = vreinterpretq_s8_u8(input);
  int8x16_t b = vdupq_n_u8(-65 + 1);
  // This will give us a logical vector for every continuation byte
  uint16x8_t lt = vreinterpretq_u16_u8(vcltq_s8(input, b));

  // One instruction computation of the bitmask. It is a uint64_t because each 
  // bit is duplicated four times.
  // See: https://community.arm.com/arm-community-blogs/b/servers-and-cloud-computing-blog/posts/porting-x86-vector-bitmask-optimizations-to-arm-neon
  uint8x8_t res = vshrn_n_u16(lt, 4);
  return vreinterpret_u64_u32(res);
}

static void print_uint8x16(const char* name, uint8x16_t vec) {
    uint8_t values[16];
    vst1q_u8(values, vec);
    
    printf("%s: ", name);
    for (int i = 0; i < 16; i++) {
        printf("%02x ", values[i]);
    }
    printf("\n");
}

// Taken from simdutf
static uint16x4_t parse_three_byte_utf8(uint8x16_t in) {
  const uint8x16_t sh = {0, 2, 3, 5, 6, 8, 9, 11, 1, 1, 4, 4, 7, 7, 10, 10};
  uint8x16_t perm = vqtbl1q_u8(in, sh);
  // Split into half vectors.
  // 10cccccc|1110aaaa
  uint8x8_t perm_low = vget_low_u8(perm); // no-op
  // 10bbbbbb|10bbbbbb
  uint8x8_t perm_high = vget_high_u8(perm);
  // xxxxxxxx 10bbbbbb
  uint16x4_t mid = vreinterpret_u16_u8(perm_high); // no-op
  // xxxxxxxx 1110aaaa
  uint16x4_t high = vreinterpret_u16_u8(perm_low); // no-op
  // Assemble with shift left insert.
  // xxxxxxaa aabbbbbb
  uint16x4_t mid_high = vsli_n_u16(mid, high, 6);
  // (perm_low << 8) | (perm_low >> 8)
  // xxxxxxxx 10cccccc
  uint16x4_t low = vreinterpret_u16_u8(vrev16_u8(perm_low));
  // Shift left insert into the low bits
  // aaaabbbb bbcccccc
  uint16x4_t composed = vsli_n_u16(low, mid_high, 6);
  return composed;
}

static inline bool in_range_u16(uint16x4_t res, uint16_t a, uint16_t b) {
  uint16x4_t lt = vclt_u16(res, vdup_n_u16(b));
  uint16x4_t gt = vcgt_u16(res, vdup_n_u16(a));
  uint16x4_t cmp = vand_u8(lt, gt);
  return vminv_u16(cmp) == 0xFFFF;
}

static void print_uint16x4(const char* name, uint16x4_t vec) {
    uint16_t values[4];
    vst1_u16(values, vec);
    
    printf("%s: ", name);
    for (int i = 0; i < 4; i++) {
        printf("%02x ", values[i]);
    }
    printf("\n");
}

// NFD normalize up to 16 bytes of UTF-8 using an end of code point mask.
// Returns the number of bytes consumed.
static size_t normalize_masked_utf8_nfd(uint8x16_t in, uint64_t mask, char *out) {
  if (mask == 0xFFFFFFFFFFFFFFFF) {
    printf("ALL ASCII\n");
    out += 16;
    return 16;
  }

  uint64_t sml_mask = mask & 0xFFFFFFFFFFFF;

  // Fast path for 4 3-byte code points (common in CJK)
  if (sml_mask == 0xF00F00F00F00) {
    printf("FOUND THREE BYTE\n");

    uint16x4_t cps = parse_three_byte_utf8(in);

    // The largest 3-byte CJK range (encompasses the vast, vast majority of CJK) 
    // that has no precomposed code points. We can just skip these.
    if (in_range_u16(cps, 0x30FF - 1, 0x9FFF + 1)) {
      /*memcpy(out, input, 12);*/
      printf("FOUND CJK\n");
      out += 12;
      return 12;
    }

    // Precomposed Hangul range. Characters in this range are algorithmically 
    // decomposable with a few arithmetic operations. They are the only 
    // precomposed characters we can decompose without a table lookup.
    //
    // Algorithm described here:
    // https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/#G59401
    if (in_range_u16(cps, 0xAC00 - 1, 0xD7AF + 1)) {
      static const uint16_t S_BASE = 0xAC00;
      static const uint16_t L_BASE = 0x1100;
      static const uint16_t V_BASE = 0x1161;
      static const uint16_t T_BASE = 0x11A7;
      static const uint16_t L_COUNT = 19;
      static const uint16_t V_COUNT = 21;
      static const uint16_t T_COUNT = 28;
      static const uint16_t N_COUNT = V_COUNT * T_COUNT;

      printf("FOUND HANGUL\n");

      // Compute the S index
      uint16x4_t s = vsub_u16(cps, vdup_n_u16(S_BASE));

      // Compute the l index: s / N_COUNT
      uint32x4_t l_fixed = vmull_u32(s, vdup_n_u16(28533));
      // Shift the fixed point number
      uint32x4_t l_wide = vshrq_n_u32(l_fixed, 24);
      uint16x4_t l = vmovn_u32(l_wide);

      // Multiply and subtract to get the remainder
      uint16x4_t v_modulo = vmls_u16(l, vdup_n_u16(N_COUNT), s);
      uint16x4_t v_shifted = vshr_n_u16(v_modulo, 2);
      uint32x4_t v_fixed = vmull_u16(v_shifted, vdup_n_u16(18725));
      uint32x4_t v_wide = vshrq_n_u32(v_fixed, 17);
      uint16x4_t v = vmovn_u32(v_wide);

      uint16x4_t t_shifted = vshr_n_u16(s, 2);
      uint32x4_t t_fixed = vmull_u16(t_shifted, vdup_n_u16(18725));
      // s / T_COUNT
      uint32x4_t t_div_wide = vshrq_n_u32(t_fixed, 17);
      uint16x4_t t_div = vmovn_u32(t_div_wide);
      uint16x4_t t = vmls_u16(t_div, vdup_n_u16(T_COUNT), s);

      // Mask for all precomposed Hangul syllables that should not have a 
      // trailing consonant
      uint16x4_t t_mask = vceqz_u16(t);

      // TODO: modify this to be the correct bitmask
      // This allows us to compute a bitmask in two instructions
      /*const uint16x8_t bitmask = { 0x0101 , 0x0202, 0x0404, 0x0808, 0x1010, 0x2020, 0x4040, 0x8080 };*/
      /*uint16x8_t mt = vandq_u16(t_mask, bitmask);*/
      /*uint16_t t_bitmask = vaddvq_u16(mt);*/

      // TODO: compute t index
      // to solve the t index byte placement problem:
      // create a logical vector that describes which bytes have t indices.
      // Turn into a bitmask (4 bits wide). Look up into a table that returns
      // a [4]u8 of where to put each L-index code point (i.e. [0, 2, 5, 7]).
      // From there can derive where to put V-index code point and T-index code point
    }
  }

  printf("UH OH\n");

  return 16;
}

static size_t normalize_utf8_nfd(char const* input, size_t length, char *out) {
  char *start = out;
  size_t p = 0;
  uint64x1_t mask1, mask2;
  // TODO: can only go when input >= 32, fall back on scalar version otherwise
  /*while (p < length) {*/
  uint8x16_t in1 = vld1q_u8((const uint8_t*)(input + p));
  uint8x16_t in2 = vld1q_u8((const uint8_t*)(input + 16 + p));

  mask1 = compute_maskq_u16(in1);
  mask2 = compute_maskq_u16(in2); 

  mask1 = vsri_n_u64(mask2, mask1, 4);
  mask2 = vshr_n_u64(mask2, 4);

  p += normalize_masked_utf8_nfd(in1, ~mask1, out);
  /*}*/

  return out - start;
}
