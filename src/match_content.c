#include "match_content.h"

#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

i64 content_search(const u8 *data, i64 len, const char *needle, i64 nlen) {
    if (nlen == 0) return 1;
    if (len < nlen) return 0;

    const u8 first = (u8)needle[0];
    const u8 *end = data + len - nlen + 1;

#ifdef __AVX2__
    {
    __m256i fv = _mm256_set1_epi8((char)first);
    while (data + 32 <= end) {
        __m256i chunk = _mm256_loadu_si256((const __m256i *)data);
        u32 mask = (u32)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, fv));
        while (mask) {
            u32 i = (u32)__builtin_ctz(mask);
            i64 j, ok = 1;
            for (j = 1; j < nlen; j++) {
                if (data[i + j] != (u8)needle[j]) { ok = 0; break; }
            }
            if (ok) return 1;
            mask &= mask - 1;
        }
        data += 32;
    }
    }
#endif

#ifdef __SSE4_2__
    {
    __m128i fv128 = _mm_set1_epi8((char)first);
    while (data + 16 <= end) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)data);
        u32 mask = (u32)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, fv128));
        while (mask) {
            u32 i = (u32)__builtin_ctz(mask);
            i64 j, ok = 1;
            for (j = 1; j < nlen; j++) {
                if (data[i + j] != (u8)needle[j]) { ok = 0; break; }
            }
            if (ok) return 1;
            mask &= mask - 1;
        }
        data += 16;
    }
    }
#endif

    for (; data < end; data++) {
        if (*data == first) {
            i64 j, ok = 1;
            for (j = 1; j < nlen; j++) {
                if (data[j] != (u8)needle[j]) { ok = 0; break; }
            }
            if (ok) return 1;
        }
    }
    return 0;
}
