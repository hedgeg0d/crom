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

static inline i64 sgr_strip(char *b, i64 n) {
    i64 w = 0;
    for (i64 r = 0; r < n; ) {
        if (b[r] == 033 && r + 1 < n && b[r+1] == '[') {
            i64 q = r + 2;
            while (q < n && !((b[q] >= 'A' && b[q] <= 'Z') ||
                              (b[q] >= 'a' && b[q] <= 'z'))) q++;
            if (q < n && b[q] == 'm') { r = q + 1; continue; }
        }
        b[w++] = b[r++];
    }
    return w;
}

static inline i64 write_str_c(i64 fd, const char *s, i64 color) {
    if (color) return write_str(fd, s);
    char buf[1024];
    i64 n = str_len(s);
    if (n > (i64)sizeof(buf)) return write_str(fd, s);
    for (i64 i = 0; i < n; i++) buf[i] = s[i];
    return write_all(fd, buf, sgr_strip(buf, n));
}

#endif
