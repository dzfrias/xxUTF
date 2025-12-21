#include "shim.h"
#include <stdint.h>
#include <unicode/unorm.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

UChar *shim_u_strFromUTF8(UChar *dest, int32_t destCapacity,
                          int32_t *pDestLength, const char *src,
                          int32_t srcLength, UErrorCode *pErrorCode) {
  return u_strFromUTF8(dest, destCapacity, pDestLength, src, srcLength,
                       pErrorCode);
}
UChar *shim_u_strToUTF8(char *dest, int32_t destCapacity, int32_t *pDestLength,
                        const UChar *src, int32_t srcLength,
                        UErrorCode *pErrorCode) {
  return u_strToUTF8(dest, destCapacity, pDestLength, src, srcLength,
                     pErrorCode);
}

const UNormalizer2 *shim_unorm2_getNFDInstance(UErrorCode *pErrorCode) {
  return unorm2_getNFDInstance(pErrorCode);
}
const UNormalizer2 *shim_unorm2_getNFCInstance(UErrorCode *pErrorCode) {
  return unorm2_getNFCInstance(pErrorCode);
}
const UNormalizer2 *shim_unorm2_getNFKDInstance(UErrorCode *pErrorCode) {
  return unorm2_getNFKDInstance(pErrorCode);
}
const UNormalizer2 *shim_unorm2_getNFKCInstance(UErrorCode *pErrorCode) {
  return unorm2_getNFKCInstance(pErrorCode);
}
int32_t shim_unorm2_normalize(const UNormalizer2 *norm2, const UChar *src,
                              int32_t length, UChar *dest, int32_t capacity,
                              UErrorCode *pErrorCode) {
  return unorm2_normalize(norm2, src, length, dest, capacity, pErrorCode);
}
