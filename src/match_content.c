#include "match_content.h"
#include "syscalls.h"
#include "util.h"

#ifdef __SSE4_2__
#include <nmmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

#define BM_THRESH 8

static void bm_build_bc(const u8 *n, i64 nl, i64 *bc) {
    for (i64 i = 0; i < 256; i++) bc[i] = nl;
    for (i64 i = 0; i < nl - 1; i++) bc[n[i]] = nl - 1 - i;
}

static i64 bm_search(const u8 *data, i64 len, const u8 *n, i64 nl) {
    i64 bc[256];
    bm_build_bc(n, nl, bc);

    i64 i = nl - 1;
    while (i < len) {
        i64 j = nl - 1, k = i;
        while (data[k] == n[j]) {
            if (j == 0) return 1;
            j--; k--;
        }
        i += bc[data[i]];
    }
    return 0;
}

static i64 simd_search(const u8 *data, i64 len, const char *needle, i64 nlen) {
    if (nlen == 1) {
        const u8 c = (u8)needle[0];
        for (i64 i = 0; i < len; i++) if (data[i] == c) return 1;
        return 0;
    }

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

i64 content_search(const u8 *data, i64 len, const char *needle, i64 nlen) {
    if (nlen == 0) return 1;
    if (len < nlen) return 0;

    if (nlen >= BM_THRESH)
        return bm_search(data, len, (const u8 *)needle, nlen);

    return simd_search(data, len, needle, nlen);
}

i64 search_file(const char *path, const char *needle, i64 nlen, u8 *rbuf, i64 rbuf_sz) {
    i64 fd = syscall3(SYS_openat, AT_FDCWD, (long)path, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;

    struct stat64 st;
    if (syscall2(SYS_fstat, fd, (long)&st) < 0) {
        syscall1(SYS_close, fd);
        return 0;
    }

    i64 size = st.st_size;
    if (size < nlen) { syscall1(SYS_close, fd); return 0; }

    if ((u64)size > (u64)rbuf_sz) {
        void *data = (void *)syscall6(SYS_mmap, 0, (unsigned long)size,
                                       PROT_READ, MAP_PRIVATE, fd, 0);
        if (is_mmap_err(data)) { syscall1(SYS_close, fd); return 0; }
        i64 found = content_search((const u8 *)data, size, needle, nlen);
        syscall2(SYS_munmap, (long)data, (unsigned long)size);
        syscall1(SYS_close, fd);
        return found;
    }

    i64 total = 0;
    while (total < size) {
        i64 n = syscall4(SYS_pread64, fd, (long)(rbuf + total),
                         (unsigned long)(size - total), total);
        if (n <= 0) { syscall1(SYS_close, fd); return 0; }
        total += n;
    }

    syscall1(SYS_close, fd);
    return content_search(rbuf, size, needle, nlen);
}
