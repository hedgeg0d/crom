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

static i32 g_bc[256];
static i64 g_bc_ready;

void content_prepare(const char *needle, i64 nlen) {
    if (nlen < BM_THRESH) return;
    const u8 *n = (const u8 *)needle;
    for (i64 i = 0; i < 256; i++) g_bc[i] = (i32)nlen;
    for (i64 i = 0; i < nlen - 1; i++) g_bc[n[i]] = (i32)(nlen - 1 - i);
    g_bc_ready = 1;
}

static i64 bm_search(const u8 *data, i64 len, const u8 *n, i64 nl) {
    i32 local[256];
    const i32 *bc = g_bc;
    if (!g_bc_ready) {
        for (i64 i = 0; i < 256; i++) local[i] = (i32)nl;
        for (i64 i = 0; i < nl - 1; i++) local[n[i]] = (i32)(nl - 1 - i);
        bc = local;
    }

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

static i64 g_skip_binary = 1;

void content_set_text_only(i64 on) { g_skip_binary = on; }

#define BIN_PROBE 4096

static i64 looks_binary(const u8 *p, i64 n) {
    if (n > BIN_PROBE) n = BIN_PROBE;
    for (i64 i = 0; i < n; i++) if (p[i] == 0) return 1;
    return 0;
}

static i64 search_open_fd(i64 fd, const char *needle, i64 nlen,
                          u8 *rbuf, i64 rbuf_sz) {
    i64 carry = 0;
    i64 first = 1;
    for (;;) {
        i64 want = rbuf_sz - carry;
        i64 n = syscall3(SYS_read, fd, (long)(rbuf + carry), (unsigned long)want);
        if (n <= 0) return 0;

        if (first) {
            first = 0;
            if (g_skip_binary && looks_binary(rbuf, n)) return 0;
        }

        i64 avail = carry + n;
        if (content_search(rbuf, avail, needle, nlen)) return 1;
        if (n < want) return 0;                 /* short read == EOF */

        carry = nlen - 1;
        if (carry > avail) carry = avail;
        for (i64 i = 0; i < carry; i++) rbuf[i] = rbuf[avail - carry + i];
    }
}

i64 search_file(const char *path, const char *needle, i64 nlen, u8 *rbuf, i64 rbuf_sz) {
    i64 fd = syscall3(SYS_openat, AT_FDCWD, (long)path, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;
    i64 found = search_open_fd(fd, needle, nlen, rbuf, rbuf_sz);
    syscall1(SYS_close, fd);
    return found;
}

i64 search_file_at(i64 dirfd, const char *name, const char *needle, i64 nlen,
                   u8 *rbuf, i64 rbuf_sz) {
    i64 fd = syscall3(SYS_openat, dirfd, (long)name, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;
    i64 found = search_open_fd(fd, needle, nlen, rbuf, rbuf_sz);
    syscall1(SYS_close, fd);
    return found;
}
