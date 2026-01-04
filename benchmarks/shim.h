#ifndef XXUTF_SHIM_H
#define XXUTF_SHIM_H

#include <stdint.h>
#include <unicode/ucasemap.h>
#include <unicode/ucnv.h>
#include <unicode/unorm.h>
#include <unicode/utypes.h>

// Zig has trouble importing ICU functions due to their macro usage in header
// files, so this small library defines shims for the necessary functions for
// `benchmark.zig`.

UChar *shim_u_strFromUTF8(UChar *dest, int32_t destCapacity,
                          int32_t *pDestLength, const char *src,
                          int32_t srcLength, UErrorCode *pErrorCode);
UChar *shim_u_strToUTF8(char *dest, int32_t destCapacity, int32_t *pDestLength,
                        const UChar *src, int32_t srcLength,
                        UErrorCode *pErrorCode);
int32_t shim_u_strFoldCase(UChar *dest, int32_t destCapacity, const UChar *src,
                           int32_t srcLength, int32_t options,
                           UErrorCode *pErrorCode);

const UNormalizer2 *shim_unorm2_getNFDInstance(UErrorCode *pErrorCode);
const UNormalizer2 *shim_unorm2_getNFCInstance(UErrorCode *pErrorCode);
const UNormalizer2 *shim_unorm2_getNFKDInstance(UErrorCode *pErrorCode);
const UNormalizer2 *shim_unorm2_getNFKCInstance(UErrorCode *pErrorCode);
int32_t shim_unorm2_normalize(const UNormalizer2 *norm2, const UChar *src,
                              int32_t length, UChar *dest, int32_t capacity,
                              UErrorCode *pErrorCode);

UConverter *shim_ucnv_open(const char *converterName, UErrorCode *err);
int32_t shim_ucnv_fromUChars(UConverter *cnv, char *dest, int32_t destCapacity,
                             const UChar *src, int32_t srcLength,
                             UErrorCode *pErrorCode);
int32_t shim_ucnv_toUChars(UConverter *cnv, UChar *dest, int32_t destCapacity,
                           const char *src, int32_t srcLength,
                           UErrorCode *pErrorCode);
void shim_ucnv_close(UConverter *cnv);

UCaseMap *shim_ucasemap_open(const char *locale, uint32_t options,
                             UErrorCode *pErrorCode);
int32_t shim_ucasemap_utf8FoldCase(const UCaseMap *csm, char *dest,
                                   int32_t destCapacity, const char *src,
                                   int32_t srcLength, UErrorCode *pErrorCode);
void shim_ucasemap_close(UCaseMap *csm);

#endif // XXUTF_SHIM_H
