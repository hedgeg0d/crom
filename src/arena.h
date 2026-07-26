#ifndef CROM_ARENA_H
#define CROM_ARENA_H

#include "types.h"
#include "syscalls.h"

static inline void *arena_alloc(Arena *a, i64 sz) {
    sz = (sz + 15) & ~15L;
    if (a->len + sz > a->cap) {
        i64 nc = a->cap ? a->cap * 2 : 4096;
        if (nc < a->len + sz) nc = a->len + sz;
        nc = (nc + 4095) & ~4095L;
        void *nb = (void *)syscall6(SYS_mmap, 0, nc, PROT_READ|PROT_WRITE,
                                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (nb == MAP_FAILED) return 0;
        if (a->buf && a->cap) {
            u8 *d = (u8 *)nb;
            u8 *s = (u8 *)a->buf;
            for (i64 i = 0; i < a->len; i++) d[i] = s[i];
            syscall2(SYS_munmap, (long)a->buf, a->cap);
        }
        a->buf = nb;
        a->cap = nc;
    }
    void *p = (u8 *)a->buf + a->len;
    a->len += sz;
    return p;
}

static inline void arena_free(Arena *a) {
    if (a->buf && a->cap) syscall2(SYS_munmap, (long)a->buf, a->cap);
    a->buf = 0; a->cap = 0; a->len = 0; a->pos = 0;
}

#endif
