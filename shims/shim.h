#ifndef ICU_SHIM_H
#define ICU_SHIM_H

#include <stdint.h>
#include <unicode/unorm.h>
#include <unicode/utypes.h>

UChar *shim_u_strFromUTF8(UChar *dest, int32_t destCapacity,
                          int32_t *pDestLength, const char *src,
                          int32_t srcLength, UErrorCode *pErrorCode);
UChar *shim_u_strToUTF8(char *dest, int32_t destCapacity, int32_t *pDestLength,
                        const UChar *src, int32_t srcLength,
                        UErrorCode *pErrorCode);

const UNormalizer2 *shim_unorm2_getNFDInstance(UErrorCode *pErrorCode);
const UNormalizer2 *shim_unorm2_getNFCInstance(UErrorCode *pErrorCode);
const UNormalizer2 *shim_unorm2_getNFKDInstance(UErrorCode *pErrorCode);
const UNormalizer2 *shim_unorm2_getNFKCInstance(UErrorCode *pErrorCode);
int32_t shim_unorm2_normalize(const UNormalizer2 *norm2, const UChar *src,
                              int32_t length, UChar *dest, int32_t capacity,
                              UErrorCode *pErrorCode);

#endif // ICU_SHIM_H
