#include "pool.h"
#include "atomics.h"
#include "syscalls.h"
#include "util.h"
#include "walk.h"
#include "match_name.h"
#include "match_content.h"
#include "display.h"

#define OBUF_SZ   16384
#define RBUF_SZ   32768
#define DBUF_SZ   32768
#define PBUF_SZ   4096
#define LSTK_CHUNK 256
#define ARENA_CAP (512L << 20)   /* reservation only; MAP_NORESERVE */

extern void clone_trampoline(void);

static volatile i64 g_out_lock;

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

typedef struct LBlk {
    struct LBlk *prev;
    DirRef it[LSTK_CHUNK];
} LBlk;

typedef struct {
    Scanner *s;
    LBlk *top;
    LBlk *freelist;
    i64 ln;
    i64 olen;
    i64 c_dirs, c_files, c_matches;
    char obuf[OBUF_SZ];
    u8 rbuf[RBUF_SZ];
} Worker;

static i64 lstk_push(Worker *w, const DirRef *d) {
    if (!w->top || w->ln == LSTK_CHUNK) {
        LBlk *b = w->freelist;
        if (b) w->freelist = b->prev;
        else b = (LBlk *)bump_alloc(&w->s->arena, sizeof(LBlk));
        if (!b) return 0;
        b->prev = w->top;
        w->top = b;
        w->ln = 0;
    }
    w->top->it[w->ln++] = *d;
    return 1;
}

static i64 lstk_pop(Worker *w, DirRef *d) {
    while (w->top && w->ln == 0) {
        LBlk *b = w->top;
        w->top = b->prev;
        b->prev = w->freelist;
        w->freelist = b;
        w->ln = w->top ? LSTK_CHUNK : 0;
    }
    if (!w->top) return 0;
    *d = w->top->it[--w->ln];
    return 1;
}

static i64 q_push(Scanner *s, const DirRef *d) {
    for (;;) {
        i64 pos = atomic_load(&s->head);
        ScanSlot *sl = &s->q[pos & (SCAN_QUEUE_CAP - 1)];
        i64 dif = atomic_load(&sl->seq) - pos;
        if (dif == 0) {
            if (atomic_cas(&s->head, pos, pos + 1) == pos) {
                sl->d = *d;
                atomic_store(&sl->seq, pos + 1);
                return 1;
            }
        } else if (dif < 0) {
            return 0;               /* full */
        }
    }
}

static i64 q_pop(Scanner *s, DirRef *d) {
    for (;;) {
        i64 pos = atomic_load(&s->tail);
        ScanSlot *sl = &s->q[pos & (SCAN_QUEUE_CAP - 1)];
        i64 dif = atomic_load(&sl->seq) - (pos + 1);
        if (dif == 0) {
            if (atomic_cas(&s->tail, pos, pos + 1) == pos) {
                *d = sl->d;
                atomic_store(&sl->seq, pos + SCAN_QUEUE_CAP);
                return 1;
            }
        } else if (dif < 0) {
            return 0;               /* empty */
        }
    }
}

static void wake_all(Scanner *s) {
    syscall6(SYS_futex, (long)&s->work_gen,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);
}

static void publish(Scanner *s, Worker *w, const DirRef *d) {
    atomic_xadd(&s->active, 1);
    if (q_push(s, d)) {
        atomic_xadd(&s->work_gen, 1);
        if (atomic_load(&s->idle) > 0) wake_all(s);
    } else if (!lstk_push(w, d)) {
        atomic_xadd(&s->active, -1);   /* arena exhausted */
    }
}

static void buf_add(char *buf, i64 *olen, const char *p, i64 plen) {
    for (i64 i = 0; i < plen; i++) buf[(*olen)++] = p[i];
}

static void buf_flush(Worker *w) {
    if (w->olen <= 0) return;
    out_lock();
    write_all(STDOUT_FILENO, w->obuf, w->olen);
    out_unlock();
    w->olen = 0;
}

static void buf_emit(Worker *w, const char *path, i64 len, i64 json) {
    if (json) {
        if (w->olen + 2 * len + 16 > OBUF_SZ) buf_flush(w);
        buf_add(w->obuf, &w->olen, "{\"path\":\"", 9);
        for (i64 i = 0; i < len; i++) {
            if (path[i] == '"' || path[i] == '\\') w->obuf[w->olen++] = '\\';
            w->obuf[w->olen++] = path[i];
        }
        buf_add(w->obuf, &w->olen, "\"}\n", 3);
    } else {
        if (w->olen + len + 1 > OBUF_SZ) buf_flush(w);
        buf_add(w->obuf, &w->olen, path, len);
        w->obuf[w->olen++] = '\n';
    }
}

static void exec_file(const char *cmd, const char *path, i64 len) {
    char buf[PBUF_SZ];
    i64 pos = 0;
    const char *c = cmd;
    while (*c && pos + len + 2 < (i64)sizeof(buf)) {
        if (*c == '{' && c[1] == '}') {
            for (i64 i = 0; i < len; i++) buf[pos++] = path[i];
            c += 2;
        } else {
            buf[pos++] = *c++;
        }
    }
    buf[pos] = 0;

    i64 pid = syscall0(SYS_fork);
    if (pid < 0) return;
    if (pid == 0) {
        const char *argv[] = {"/bin/sh", "-c", buf, 0};
        const char *envp[] = {"PATH=/usr/bin:/bin:/usr/sbin", 0};
        syscall3(SYS_execve, (long)"/bin/sh", (long)argv, (long)envp);
        syscall1(SYS_exit, 1);
    }
    i32 status;
    syscall4(SYS_wait4, pid, (long)&status, 0, 0);
}

static i64 size_pass(const ScanCfg *c, i64 dirfd, const char *name) {
    i32 fd = (i32)syscall3(SYS_openat, dirfd, (long)name, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat64 st;
    i64 ok = syscall2(SYS_fstat, fd, (long)&st) >= 0;
    if (ok) ok = c->size_cmp > 0 ? st.st_size > c->size_val
                                 : st.st_size < c->size_val;
    syscall1(SYS_close, fd);
    return ok;
}

static void consider(Scanner *s, Worker *w, i64 dirfd, const char *path,
                     i64 len, const char *name, u8 type) {
    const ScanCfg *c = s->cfg;

    if (c->type_filter) {
        if (c->type_filter == 'f' && type != DT_REG) return;
        if (c->type_filter == 'd' && type != DT_DIR) return;
        if (c->type_filter == 'l' && type != DT_LNK) return;
    } else if (type != DT_REG) {
        return;
    }

    if (c->pattern && !match_glob(c->pattern, name)) return;
    if (c->size_cmp && !size_pass(c, dirfd, name)) return;
    if (c->needle &&
        !search_file_at(dirfd, name, c->needle, c->needle_len,
                        w->rbuf, RBUF_SZ)) return;

    w->c_matches++;

    if (c->exec_cmd) {
        out_lock();
        exec_file(c->exec_cmd, path, len);
        out_unlock();
    } else {
        buf_emit(w, path, len, c->json_out);
    }
}

static void scan_dir(Scanner *s, Worker *w, const DirRef *d) {
    i64 fd = syscall3(SYS_openat, AT_FDCWD, (long)d->path,
                      O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (fd < 0) return;

    const ScanCfg *c = s->cfg;
    const GitNode *ign = d->ign;
    if (c->use_ignore)
        ign = gitignore_load(&s->arena, ign, d->path, d->len);

    char pbuf[PBUF_SZ];
    i64 plen = d->len;
    if (plen >= PBUF_SZ - 2) { syscall1(SYS_close, fd); return; }
    for (i64 i = 0; i < plen; i++) pbuf[i] = d->path[i];
    if (plen == 0) pbuf[plen++] = '.';
    if (pbuf[plen - 1] != '/') pbuf[plen++] = '/';
    i64 prefix = plen;

    u8 dbuf[DBUF_SZ];
    for (;;) {
        i64 n = syscall3(SYS_getdents64, fd, (long)dbuf, DBUF_SZ);
        if (n <= 0) break;

        for (i64 pos = 0; pos < n; ) {
            struct linux_dirent64 *e = (struct linux_dirent64 *)(dbuf + pos);
            pos += e->d_reclen;

            const char *name = e->d_name;
            if (name[0] == '.') {
                if (name[1] == 0) continue;
                if (name[1] == '.' && name[2] == 0) continue;
            }

            u8 type = e->d_type;
            i32 is_dir = (type == DT_DIR);
            if (is_dir && match_ignore(name, 1)) continue;
            if (c->use_ignore && gitignore_check(ign, name, is_dir)) continue;

            i64 nl = str_len(name);
            if (prefix + nl >= PBUF_SZ) continue;
            for (i64 i = 0; i < nl; i++) pbuf[prefix + i] = name[i];
            i64 flen = prefix + nl;
            pbuf[flen] = 0;

            if (is_dir) {
                w->c_dirs++;
                if (c->bar && (w->c_dirs & 63) == 0) {
                    display_update(atomic_load(&s->n_dirs) + w->c_dirs,
                                   atomic_load(&s->n_files) + w->c_files,
                                   atomic_load(&s->n_matches) + w->c_matches);
                }

                i64 cd = d->depth + 1;
                if (c->max_depth < 0 || cd <= c->max_depth) {
                    char *cp = (char *)bump_alloc(&s->arena, flen + 1);
                    if (cp) {
                        for (i64 i = 0; i <= flen; i++) cp[i] = pbuf[i];
                        DirRef sub;
                        sub.path = cp;
                        sub.len = (i32)flen;
                        sub.depth = (i32)cd;
                        sub.ign = ign;
                        publish(s, w, &sub);
                    }
                }
            } else if (type == DT_REG) {
                w->c_files++;
            }

            consider(s, w, fd, pbuf, flen, name, type);
        }
    }

    syscall1(SYS_close, fd);
}

static void worker_loop(Scanner *s, Worker *w) {
    for (;;) {
        DirRef d;

        if (lstk_pop(w, &d) || q_pop(s, &d)) {
            scan_dir(s, w, &d);
            if (atomic_xadd(&s->active, -1) == 1) {
                atomic_store(&s->done, 1);
                atomic_xadd(&s->work_gen, 1);
                wake_all(s);
            }
            continue;
        }

        if (atomic_load(&s->done)) break;

        atomic_xadd(&s->idle, 1);
        i64 gen = atomic_load(&s->work_gen);
        if (q_pop(s, &d)) {
            atomic_xadd(&s->idle, -1);
            scan_dir(s, w, &d);
            if (atomic_xadd(&s->active, -1) == 1) {
                atomic_store(&s->done, 1);
                atomic_xadd(&s->work_gen, 1);
                wake_all(s);
            }
            continue;
        }
        if (!atomic_load(&s->done) && atomic_load(&s->work_gen) == gen)
            syscall6(SYS_futex, (long)&s->work_gen,
                     FUTEX_WAIT|FUTEX_PRIVATE_FLAG, (i32)gen, 0, 0, 0);
        atomic_xadd(&s->idle, -1);
    }
}

static void worker_init(Worker *w, Scanner *s) {
    w->s = s; w->top = 0; w->freelist = 0; w->ln = 0; w->olen = 0;
    w->c_dirs = 0; w->c_files = 0; w->c_matches = 0;
}

static void worker_finish(Worker *w) {
    buf_flush(w);
    atomic_xadd(&w->s->n_dirs, w->c_dirs);
    atomic_xadd(&w->s->n_files, w->c_files);
    atomic_xadd(&w->s->n_matches, w->c_matches);
}

static i64 worker_main(void *arg) {
    Scanner *s = (Scanner *)arg;
    Worker w;
    worker_init(&w, s);

    worker_loop(s, &w);
    worker_finish(&w);

    atomic_xadd(&s->workers_exited, 1);
    syscall6(SYS_futex, (long)&s->workers_exited,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);
    return 0;
}

static i64 spawn_worker(Scanner *s) {
    i64 stksz = 1048576;
    void *stk = (void *)syscall6(SYS_mmap, 0, (unsigned long)stksz,
                                 PROT_READ|PROT_WRITE,
                                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (is_mmap_err(stk)) return -1;

    u64 sp = (u64)stk + stksz;
    sp &= ~15ULL;
    sp -= 24;
    ((void **)(u64)sp)[0] = (void *)clone_trampoline;
    ((void **)(u64)sp)[1] = s;
    ((void **)(u64)sp)[2] = (void *)worker_main;

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

i64 scan_run(Scanner *s, const ScanCfg *cfg, const char *root) {
    s->cfg = cfg;
    s->head = 0; s->tail = 0;
    s->active = 0; s->done = 0; s->work_gen = 0; s->idle = 0;
    s->n_dirs = 0; s->n_files = 0; s->n_matches = 0;
    s->workers_live = 0; s->workers_exited = 0;
    g_out_lock = 0;

    for (i64 i = 0; i < SCAN_QUEUE_CAP; i++) s->q[i].seq = i;

    if (bump_init(&s->arena, ARENA_CAP) < 0) return 0;

    if (!root || !root[0]) root = ".";
    i64 rl = str_len(root);
    char *rp = (char *)bump_alloc(&s->arena, rl + 1);
    if (!rp) return 0;
    for (i64 i = 0; i <= rl; i++) rp[i] = root[i];

    DirRef r;
    r.path = rp; r.len = (i32)rl; r.depth = 0; r.ign = 0;
    atomic_xadd(&s->active, 1);
    if (!q_push(s, &r)) return 0;

    for (i64 i = 1; i < cfg->num_workers; i++)
        if (spawn_worker(s) == 0) atomic_xadd(&s->workers_live, 1);

    Worker w;
    worker_init(&w, s);
    worker_loop(s, &w);
    worker_finish(&w);

    i64 live = atomic_load(&s->workers_live);
    while (atomic_load(&s->workers_exited) < live) {
        atomic_store(&s->done, 1);
        atomic_xadd(&s->work_gen, 1);
        wake_all(s);
        syscall1(SYS_sched_yield, 0);
    }

    return atomic_load(&s->n_matches);
}
