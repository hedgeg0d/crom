#include "pool.h"
#include "atomics.h"
#include "syscalls.h"
#include "util.h"
#include "match_content.h"

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

static i64 search_file(const char *path) {
    i64 fd = syscall3(SYS_openat, AT_FDCWD, (long)path, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;

    i64 size = syscall3(SYS_lseek, fd, 0, SEEK_END);
    if (size <= 0) { syscall1(SYS_close, fd); return 0; }

    void *data = (void *)syscall6(SYS_mmap, 0, (unsigned long)size,
                                   PROT_READ, MAP_PRIVATE, fd, 0);
    if (is_mmap_err(data)) { syscall1(SYS_close, fd); return 0; }

    i64 found = content_search((const u8 *)data, size, g_needle, g_nlen);

    syscall2(SYS_munmap, (long)data, (unsigned long)size);
    syscall1(SYS_close, fd);
    return found;
}

void pool_init(Pool *p, i64 workers) {
    p->head = 0;
    p->tail = 0;
    p->done = 0;
    p->pending = 0;
    p->matches = 0;
    p->num_workers = workers > 0 ? workers : 1;
}

static i64 pop_item(Pool *p, char **path, i64 *len, u8 *dtype) {
    for (;;) {
        i64 t = atomic_load(&p->tail);
        i64 h = atomic_load(&p->head);

        if (t >= h) {
            if (atomic_load(&p->done)) return -1;
            syscall6(SYS_futex, (long)&p->head,
                     FUTEX_WAIT|FUTEX_PRIVATE_FLAG, h, 0, 0, 0);
            continue;
        }

        if (atomic_cas(&p->tail, t, t + 1) != t) continue;

        PoolItem *it = &p->items[t & (POOL_QUEUE_CAP - 1)];
        *path = it->path;
        *len = it->len;
        *dtype = it->dtype;
        atomic_xadd(&p->pending, 1);
        return 0;
    }
}

i64 pool_push(Pool *p, const char *path, i64 len, u8 dtype) {
    if (len >= POOL_PATH_SZ) return -1;

    for (;;) {
        i64 h = atomic_load(&p->head);
        i64 t = atomic_load(&p->tail);
        if (h - t >= POOL_QUEUE_CAP) {
            syscall1(SYS_sched_yield, 0);
            continue;
        }

        PoolItem *it = &p->items[h & (POOL_QUEUE_CAP - 1)];
        for (i64 i = 0; i < len; i++) it->path[i] = path[i];
        it->path[len] = 0;
        it->len = len;
        it->dtype = dtype;

        atomic_store(&p->head, h + 1);

        if (h == t) {
            syscall6(SYS_futex, (long)&p->head,
                     FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 1, 0, 0, 0);
        }
        return 0;
    }
}

static i64 worker_fn(void *arg) {
    Pool *p = (Pool *)arg;

    for (;;) {
        char *path; i64 len; u8 dtype;
        if (pop_item(p, &path, &len, &dtype) < 0) break;

        i64 match = 1;
        if (g_needle) match = search_file(path);

        if (match) {
            atomic_xadd(&p->matches, 1);
            out_lock();
            write_all(STDOUT_FILENO, path, len);
            write_all(STDOUT_FILENO, "\n", 1);
            out_unlock();
        }

        atomic_xadd(&p->pending, -1);
    }

    return 0;
}

static void spawn_worker(Pool *p) {
    i64 stksz = 65536;
    void *stk = (void *)syscall6(SYS_mmap, 0, (unsigned long)stksz,
                                  PROT_READ|PROT_WRITE,
                                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (is_mmap_err(stk)) return;

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
    }
}

void pool_spawn(Pool *p, const char *needle, i64 nlen) {
    g_needle = needle;
    g_nlen = nlen;
    g_out_lock = 0;

    for (i64 i = 0; i < p->num_workers; i++) {
        spawn_worker(p);
    }
}

void pool_flush(Pool *p) {
    atomic_store(&p->done, 1);
    syscall6(SYS_futex, (long)&p->head,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);

    for (;;) {
        i64 h = atomic_load(&p->head);
        i64 t = atomic_load(&p->tail);
        i64 pend = atomic_load(&p->pending);
        if (h == t && pend == 0) break;
        syscall1(SYS_sched_yield, 0);
    }
}
