#include "pool.h"
#include "atomics.h"
#include "syscalls.h"
#include "util.h"
#include "match_content.h"
#include "iouring.h"

static const char *g_needle;
static i64 g_nlen;

static volatile i64 g_out_lock;

extern void clone_trampoline(void);

static void out_lock(void) {
    while (atomic_cas(&g_out_lock, 0, 1) != 0) {
        syscall6(SYS_futex, (long)&g_out_lock,
                 FUTEX_WAIT|FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
    }
}

static void out_unlock(void) {
    atomic_store(&g_out_lock, 0);
    syscall6(SYS_futex, (long)&g_out_lock,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
}

void pool_init(Pool *p, i64 workers) {
    p->head = 0;
    p->tail = 0;
    p->done = 0;
    p->pending = 0;
    p->matches = 0;
    p->workers_live = 0;
    p->workers_exited = 0;
    p->num_workers = workers > 0 ? workers : 1;

    for (i64 i = 0; i < POOL_QUEUE_CAP; i++) p->items[i].seq = i;
}

/* Claimed slot t -> copy payload out, then hand the slot back to the
   producer. Must run before returning, since the producer is free to refill
   the slot the moment seq is bumped. */
static void slot_take(Pool *p, i64 t, char *path, i64 *len, u8 *dtype) {
    PoolItem *it = &p->items[t & (POOL_QUEUE_CAP - 1)];

    while (atomic_load(&it->seq) != t + 1) syscall1(SYS_sched_yield, 0);

    i64 n = it->len;
    for (i64 i = 0; i < n; i++) path[i] = it->path[i];
    path[n] = 0;
    *len = n;
    *dtype = it->dtype;

    atomic_store(&it->seq, t + POOL_QUEUE_CAP);
}

i64 pool_pop(Pool *p, char *path, i64 *len, u8 *dtype) {
    for (;;) {
        i64 t = atomic_load(&p->tail);
        i64 h = atomic_load(&p->head);

        if (t >= h) {
            if (atomic_load(&p->done)) return -1;
            atomic_xadd(&p->waiters, 1);
            /* Re-check after registering: done/head may have changed between
               the checks above and the sleep, and that wake would be lost. */
            if (!atomic_load(&p->done) && atomic_load(&p->head) == h)
                syscall6(SYS_futex, (long)&p->head,
                         FUTEX_WAIT|FUTEX_PRIVATE_FLAG, h, 0, 0, 0);
            atomic_xadd(&p->waiters, -1);
            continue;
        }

        if (atomic_cas(&p->tail, t, t + 1) != t) continue;

        atomic_xadd(&p->pending, 1);
        slot_take(p, t, path, len, dtype);
        return 0;
    }
}

i64 pool_try_pop(Pool *p, char *path, i64 *len, u8 *dtype) {
    for (;;) {
        i64 t = atomic_load(&p->tail);
        i64 h = atomic_load(&p->head);

        if (t >= h) return -1;

        if (atomic_cas(&p->tail, t, t + 1) != t) continue;

        atomic_xadd(&p->pending, 1);
        slot_take(p, t, path, len, dtype);
        return 0;
    }
}

i64 pool_push(Pool *p, const char *path, i64 len, u8 dtype) {
    if (len >= POOL_PATH_SZ) return -1;

    i64 h = atomic_load(&p->head);
    PoolItem *it = &p->items[h & (POOL_QUEUE_CAP - 1)];

    /* Free only once the previous occupant's consumer has copied out. */
    while (atomic_load(&it->seq) != h) syscall1(SYS_sched_yield, 0);

    for (i64 i = 0; i < len; i++) it->path[i] = path[i];
    it->path[len] = 0;
    it->len = len;
    it->dtype = dtype;

    atomic_store(&it->seq, h + 1);
    atomic_store(&p->head, h + 1);

    if (atomic_load(&p->waiters) > 0)
        syscall6(SYS_futex, (long)&p->head,
                 FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
    return 0;
}

#define OBUF_SZ 16384
#define BATCH_SZ 32

static void buf_add(char *buf, i64 *olen, const char *p, i64 plen) {
    for (i64 i = 0; i < plen; i++) buf[(*olen)++] = p[i];
}

static void buf_emit(Pool *p, const char *path, i64 len, char *obuf, i64 *olen) {
    if (p->json_out) {
        if (*olen + len + 16 > OBUF_SZ) {
            out_lock();
            write_all(STDOUT_FILENO, obuf, *olen);
            out_unlock();
            *olen = 0;
        }
        buf_add(obuf, olen, "{\"path\":\"", 9);
        for (i64 i = 0; i < len; i++) {
            if (path[i] == '"' || path[i] == '\\') obuf[(*olen)++] = '\\';
            obuf[(*olen)++] = path[i];
        }
        buf_add(obuf, olen, "\"}\n", 3);
    } else {
        if (*olen + len + 1 > OBUF_SZ) {
            out_lock();
            write_all(STDOUT_FILENO, obuf, *olen);
            out_unlock();
            *olen = 0;
        }
        buf_add(obuf, olen, path, len);
        obuf[(*olen)++] = '\n';
    }
}

typedef struct {
    char path[POOL_PATH_SZ];
    i64 len;
} BatchSlot;

/* Each file is one chain of three linked SQEs. user_data carries the batch
   index and which link it came from, since every SQE posts its own CQE. */
#define UD_MAKE(i, kind) (((u64)(i) << 2) | (u64)(kind))
#define UD_IDX(ud)       ((i64)((ud) >> 2))
#define UD_KIND(ud)      ((u32)((ud) & 3))
#define UD_OPEN  0
#define UD_READ  1
#define UD_CLOSE 2

#define RBUF_SZ  32768
#define SQE_PER_FILE 3
/* Out of band for a read result, which is either a byte count or -errno.
   -1 cannot be used: that is a legitimate -EPERM from the open. */
#define RRES_SYNC 0x7fffffff

static i64 worker_fn(void *arg) {
    Pool *p = (Pool *)arg;
    char obuf[OBUF_SZ];
    i64 olen = 0;
    IOUring ring;
    i64 use_ring = 0;
    u8 *rbufs = 0;

#ifndef CROM_NO_URING
    /* Ring must hold a whole batch of chains, plus a registered file table
       with one direct-descriptor slot per in-flight file. */
    if (iouring_init(&ring, BATCH_SZ * SQE_PER_FILE) == 0) {
        if (iouring_register_sparse_files(&ring, BATCH_SZ) == 0) {
            rbufs = (u8 *)syscall6(SYS_mmap, 0, BATCH_SZ * RBUF_SZ,
                                   PROT_READ|PROT_WRITE,
                                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
            if (is_mmap_err(rbufs)) rbufs = 0;
            else use_ring = 1;
        }
        if (!use_ring) iouring_free(&ring);
    }
#endif

    if (!use_ring) {
        u8 rbuf[RBUF_SZ];
        for (;;) {
            char path[POOL_PATH_SZ]; i64 len; u8 dtype;
            if (pool_pop(p, path, &len, &dtype) < 0) break;
            i64 match = 1;
            if (g_needle)
                match = search_file(path, g_needle, g_nlen, rbuf, sizeof(rbuf));
            if (match) {
                atomic_xadd(&p->matches, 1);
                buf_emit(p, path, len, obuf, &olen);
            }
            atomic_xadd(&p->pending, -1);
        }
        goto done;
    }

    {
    BatchSlot batch[BATCH_SZ];
    i32 rres[BATCH_SZ];

    for (;;) {
        u8 dtype;
        if (pool_pop(p, batch[0].path, &batch[0].len, &dtype) < 0)
            break;
        i64 count = 1;

        while (count < BATCH_SZ) {
            if (pool_try_pop(p, batch[count].path, &batch[count].len, &dtype) < 0)
                break;
            count++;
        }

        for (i64 i = 0; i < count; i++) rres[i] = RRES_SYNC;

        i64 staged = 0;
        for (i64 i = 0; i < count; i++) {
            if (iouring_sq_space(&ring) < SQE_PER_FILE) break;

            /* OPENAT into direct-descriptor slot i (file_index is slot+1).
               IO_LINK: if the open fails, the read and close are cancelled. */
            struct iouring_sqe *o = iouring_get_sqe(&ring);
            o->opcode = IORING_OP_OPENAT;
            o->flags = IOSQE_IO_LINK;
            o->fd = AT_FDCWD;
            o->addr = (u64)batch[i].path;
            /* io_openat_prep rejects O_CLOEXEC together with file_index:
               a direct descriptor never lands in the fd table. */
            o->open_flags = O_RDONLY;
            o->file_index = (u32)i + 1;
            o->user_data = UD_MAKE(i, UD_OPEN);

            /* READ addresses that slot via FIXED_FILE. HARDLINK so the close
               still runs even when the read errors (e.g. EISDIR). */
            struct iouring_sqe *rd = iouring_get_sqe(&ring);
            rd->opcode = IORING_OP_READ;
            rd->flags = IOSQE_FIXED_FILE|IOSQE_IO_HARDLINK;
            rd->fd = (i32)i;
            rd->addr = (u64)(rbufs + i * RBUF_SZ);
            rd->len = RBUF_SZ;
            rd->off = 0;
            rd->user_data = UD_MAKE(i, UD_READ);

            /* Direct close: fd must be 0 and the slot goes in file_index.
               FIXED_FILE is rejected by close_prep, so flags stay clear. */
            struct iouring_sqe *c = iouring_get_sqe(&ring);
            c->opcode = IORING_OP_CLOSE;
            c->fd = 0;
            c->file_index = (u32)i + 1;
            c->user_data = UD_MAKE(i, UD_CLOSE);

            staged++;
        }

        u32 nsqe = (u32)(staged * SQE_PER_FILE);
        i32 got = nsqe ? iouring_submit_and_wait(&ring, nsqe) : 0;

        /* Drain whatever the ring produced; one CQE per SQE, chains included. */
        for (i32 n = 0; n < got; n++) {
            struct iouring_cqe *cqe;
            if (!iouring_peek_cqe(&ring, &cqe)) {
                if (iouring_wait(&ring, 1) < 0) break;
                if (!iouring_peek_cqe(&ring, &cqe)) break;
            }
            i32 res = cqe->res;
            u64 ud = cqe->user_data;
            iouring_cqe_seen(&ring, cqe);

            i64 idx = UD_IDX(ud);
            if (UD_KIND(ud) == UD_READ && idx >= 0 && idx < count)
                rres[idx] = res;
        }

        /* A partial submit could split a chain, so treat it as "all sync". */
        if (got != (i32)nsqe)
            for (i64 i = 0; i < count; i++) rres[i] = RRES_SYNC;

        for (i64 i = 0; i < count; i++) {
            i64 match = 0;
            i32 n = rres[i];

            if (n >= 0 && n < RBUF_SZ) {
                /* Whole file is in the buffer: a short read on a regular file
                   only happens at EOF. */
                match = content_search(rbufs + i * RBUF_SZ, n, g_needle, g_nlen);
            } else if (n == RBUF_SZ || n == RRES_SYNC) {
                /* Buffer filled means the file may be longer than RBUF_SZ;
                   RRES_SYNC means it never went through the ring. Both need
                   the synchronous path, which mmaps anything oversized. */
                u8 *scratch = rbufs + i * RBUF_SZ;
                match = search_file(batch[i].path, g_needle, g_nlen,
                                    scratch, RBUF_SZ);
            }
            /* Anything else is a real -errno from open/read: not a match. */

            if (match) {
                atomic_xadd(&p->matches, 1);
                buf_emit(p, batch[i].path, batch[i].len, obuf, &olen);
            }
            atomic_xadd(&p->pending, -1);
        }
    }
    }

    syscall2(SYS_munmap, (long)rbufs, BATCH_SZ * RBUF_SZ);
    iouring_free(&ring);

done:
    if (olen > 0) {
        out_lock();
        write_all(STDOUT_FILENO, obuf, olen);
        out_unlock();
    }

    /* Publish the flush before pool_flush() is allowed to let main exit. */
    atomic_xadd(&p->workers_exited, 1);
    syscall6(SYS_futex, (long)&p->workers_exited,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);
    return 0;
}

static i64 spawn_worker(Pool *p) {
    /* worker_fn's frame is large: rbuf 32K + obuf 16K + batch 33K. */
    i64 stksz = 1048576;
    void *stk = (void *)syscall6(SYS_mmap, 0, (unsigned long)stksz,
                                  PROT_READ|PROT_WRITE,
                                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (is_mmap_err(stk)) return -1;

    u64 sp = (u64)stk + stksz;
    sp &= ~15ULL;
    sp -= 24;
    ((void **)(u64)sp)[0] = (void *)clone_trampoline;
    ((void **)(u64)sp)[1] = p;
    ((void **)(u64)sp)[2] = (void *)worker_fn;

    long flags = CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|
                 CLONE_THREAD|CLONE_SETTLS|CLONE_PARENT_SETTID|
                 CLONE_CHILD_CLEARTID;

    long tid;
    __asm__ volatile (
        "mov %[flags], %%rdi\n"
        "mov %[stack], %%rsi\n"
        "xor %%edx, %%edx\n"
        "xor %%r10d, %%r10d\n"
        "xor %%r8d, %%r8d\n"
        "mov $56, %%rax\n"
        "syscall\n"
        "test %%rax, %%rax\n"
        "jnz 1f\n"
        "ret\n"
        "1:"
        : "=a"(tid)
        : [flags] "r"(flags), [stack] "r"(sp)
        : "rdi", "rsi", "rdx", "r10", "r8", "rcx", "r11", "memory"
    );

    if (tid < 0) {
        syscall2(SYS_munmap, (long)stk, (unsigned long)stksz);
        return -1;
    }
    return 0;
}

void pool_spawn(Pool *p, const char *needle, i64 nlen) {
    g_needle = needle;
    g_nlen = nlen;
    g_out_lock = 0;

    for (i64 i = 0; i < p->num_workers; i++) {
        if (spawn_worker(p) == 0) atomic_xadd(&p->workers_live, 1);
    }
}

void pool_flush(Pool *p) {
    atomic_store(&p->done, 1);

    /* Wait for every worker to drain its private output buffer and exit.
       Waiting on pending==0 alone is not enough: a worker decrements pending
       while the matching path is still sitting in its 16K obuf, and returning
       here lets main exit_group() and kill it before the flush. */
    i64 live = atomic_load(&p->workers_live);
    for (;;) {
        /* Re-broadcast: a worker may sample head, then have done set, then
           enter FUTEX_WAIT after our first wake -- that wake would be lost. */
        syscall6(SYS_futex, (long)&p->head,
                 FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);

        if (atomic_load(&p->workers_exited) >= live) break;
        syscall1(SYS_sched_yield, 0);
    }
}
