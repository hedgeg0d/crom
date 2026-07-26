#ifndef CROM_ARENA_H
#define CROM_ARENA_H

#include "types.h"
#include "syscalls.h"

static inline void *arena_alloc(Arena *a, i64 sz) {
    sz = (sz + 15) & ~15L;
    if (a->len + sz > a->cap) {
        i64 nc = a->cap ? a->cap * 2 : 65536;
        if (nc < a->len + sz) nc = a->len + sz;
        nc = (nc + 4095) & ~4095L;
        void *nb;
        if (a->buf) {
            nb = (void *)syscall5(SYS_mremap, (long)a->buf,
                                   (unsigned long)a->cap,
                                   (unsigned long)nc, 0, 0);
        } else {
            nb = (void *)syscall6(SYS_mmap, 0, (unsigned long)nc,
                                   PROT_READ|PROT_WRITE,
                                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        }
        if (is_mmap_err(nb)) return 0;
        a->buf = nb;
        a->cap = nc;
    }
    void *p = (u8 *)a->buf + a->len;
    a->len += sz;
    return p;
}

static inline void arena_free(Arena *a) {
    if (a->buf && a->cap)
        syscall2(SYS_munmap, (long)a->buf, (unsigned long)a->cap);
    a->buf = 0; a->cap = 0; a->len = 0; a->pos = 0;
}

#endif
