#include "shim.h"
#include <stdint.h>
#include <unicode/ucnv.h>
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
int32_t shim_u_strFoldCase(UChar *dest, int32_t destCapacity, const UChar *src,
                           int32_t srcLength, int32_t options,
                           UErrorCode *pErrorCode) {
  return u_strFoldCase(dest, destCapacity, src, srcLength, options, pErrorCode);
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

UConverter *shim_ucnv_open(const char *converterName, UErrorCode *err) {
  return ucnv_open(converterName, err);
}
int32_t shim_ucnv_fromUChars(UConverter *cnv, char *dest, int32_t destCapacity,
                             const UChar *src, int32_t srcLength,
                             UErrorCode *pErrorCode) {
  return ucnv_fromUChars(cnv, dest, destCapacity, src, srcLength, pErrorCode);
}
int32_t shim_ucnv_toUChars(UConverter *cnv, UChar *dest, int32_t destCapacity,
                           const char *src, int32_t srcLength,
                           UErrorCode *pErrorCode) {
  return ucnv_toUChars(cnv, dest, destCapacity, src, srcLength, pErrorCode);
}
void shim_ucnv_close(UConverter *cnv) { ucnv_close(cnv); }

UCaseMap *shim_ucasemap_open(const char *locale, uint32_t options,
                             UErrorCode *pErrorCode) {
  return ucasemap_open(locale, options, pErrorCode);
}
int32_t shim_ucasemap_utf8FoldCase(const UCaseMap *csm, char *dest,
                                   int32_t destCapacity, const char *src,
                                   int32_t srcLength, UErrorCode *pErrorCode) {
  return ucasemap_utf8FoldCase(csm, dest, destCapacity, src, srcLength,
                               pErrorCode);
}
void shim_ucasemap_close(UCaseMap *csm) { ucasemap_close(csm); }
