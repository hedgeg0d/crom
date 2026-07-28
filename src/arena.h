#ifndef CROM_ARENA_H
#define CROM_ARENA_H

#include "types.h"
#include "syscalls.h"
#include "atomics.h"

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x4000
#endif

/* Lock-free bump allocator over one big MAP_NORESERVE reservation: the
   mapping costs address space, not memory, so workers can share it without
   ever needing to grow (and therefore without needing a lock). */
typedef struct {
    u8 *base;
    volatile i64 off;
    i64 cap;
} Bump;

static inline i64 bump_init(Bump *b, i64 cap) {
    void *p = (void *)syscall6(SYS_mmap, 0, (unsigned long)cap,
                               PROT_READ|PROT_WRITE,
                               MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    if (is_mmap_err(p)) return -1;
    b->base = (u8 *)p;
    b->off = 0;
    b->cap = cap;
    return 0;
}

static inline void *bump_alloc(Bump *b, i64 n) {
    n = (n + 15) & ~15L;
    i64 o = atomic_xadd(&b->off, n);
    if (o + n > b->cap) return 0;
    return b->base + o;
}

static inline void bump_free(Bump *b) {
    if (b->base) syscall2(SYS_munmap, (long)b->base, (unsigned long)b->cap);
    b->base = 0; b->off = 0; b->cap = 0;
}

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
