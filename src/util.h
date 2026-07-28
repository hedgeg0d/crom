#ifndef CROM_UTIL_H
#define CROM_UTIL_H

#include "types.h"
#include "syscalls.h"

static inline i64 str_len(const char *s) {
    const char *p = s;
    while (*p) p++;
    return p - s;
}

static inline i64 str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static inline i64 write_all(i64 fd, const char *s, i64 len) {
    i64 off = 0;
    while (off < len) {
        i64 n = syscall3(SYS_write, fd, (long)(s + off), (unsigned long)(len - off));
        if (n < 0) return -1;
        off += n;
    }
    return off;
}

static inline i64 write_str(i64 fd, const char *s) {
    return write_all(fd, s, str_len(s));
}

static inline i64 is_tty(i64 fd) {
    char termios_buf[64];
    return syscall3(SYS_ioctl, fd, TCGETS, (long)termios_buf) == 0;
}

static inline void *crom_memcpy(void *dst, const void *src, i64 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (i64 i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

#endif
