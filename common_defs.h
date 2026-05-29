#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#define XXUTF_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define XXUTF_LIKELY(x) __builtin_expect(!!(x), 1)

#ifdef NDEBUG
#define XXUTF_ASSERT(x) ((void)0)
#else
#include <assert.h>
#define XXUTF_ASSERT(x) assert(x)
#endif

#define XXUTF_ALWAYS_INLINE __attribute__((always_inline)) inline
#define XXUTF_UNUSED __attribute__((unused))

#endif // COMMON_DEFS_H
