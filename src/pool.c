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
    WCount *wc;
    i64 c_dirs, c_files, c_matches;
    char obuf[OBUF_SZ] __attribute__((aligned(64)));
    u8 rbuf[RBUF_SZ] __attribute__((aligned(64)));
} __attribute__((aligned(64))) Worker;

static void wc_publish(Worker *w) {
    w->wc->dirs = w->c_dirs;
    w->wc->files = w->c_files;
    w->wc->matches = w->c_matches;
}

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

/* Sequence counters are biased by slot index so the zero-filled BSS is already
   a valid empty ring; initialising it cost 32 KB of page faults per run. */
static inline i64 slot_seq(ScanSlot *sl, i64 idx) {
    return atomic_load(&sl->seq) + idx;
}

static inline void slot_publish(ScanSlot *sl, i64 idx, i64 seq) {
    atomic_store(&sl->seq, seq - idx);
}

static i64 q_push(Scanner *s, const DirRef *d) {
    for (;;) {
        i64 pos = atomic_load(&s->head);
        i64 idx = pos & (SCAN_QUEUE_CAP - 1);
        ScanSlot *sl = &s->q[idx];
        i64 dif = slot_seq(sl, idx) - pos;
        if (dif == 0) {
            if (atomic_cas(&s->head, pos, pos + 1) == pos) {
                sl->d = *d;
                slot_publish(sl, idx, pos + 1);
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
        i64 idx = pos & (SCAN_QUEUE_CAP - 1);
        ScanSlot *sl = &s->q[idx];
        i64 dif = slot_seq(sl, idx) - (pos + 1);
        if (dif == 0) {
            if (atomic_cas(&s->tail, pos, pos + 1) == pos) {
                *d = sl->d;
                slot_publish(sl, idx, pos + SCAN_QUEUE_CAP);
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

static void scan_finish(Scanner *s) {
    atomic_store(&s->done, 1);
    atomic_xadd(&s->work_gen, 1);
    wake_all(s);
    syscall6(SYS_futex, (long)&s->done,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);
}

static i64 spawn_thread(i64 (*fn)(void *), void *arg);
static i64 worker_main(void *arg);

/* Fault the ring in before the first helper: a producer stalling inside q_push
   stalls everyone spinning on it. `+= 0` keeps the two live entries intact. */
static void q_prefault(Scanner *s) {
    for (i64 i = 0; i < SCAN_QUEUE_CAP; i++) s->q[i].seq += 0;
}

/* No clone() until the ring holds work for someone else; then the whole pool
   goes up at once, since ramping one thread per publish delayed full width. */
static void maybe_spawn(Scanner *s) {
    i64 want = s->cfg->num_workers - 1;          /* the caller is worker 0 */
    if (want <= 0) return;

    for (;;) {
        if (atomic_load(&s->spawned) >= want) return;
        if (atomic_load(&s->head) - atomic_load(&s->tail) < 2) return;
        i64 mine = atomic_xadd(&s->spawned, 1);
        if (mine >= want) return;                          /* lost the race */
        if (mine == 0) q_prefault(s);

        /* Counted before the clone, or scan_run could stop waiting on a thread
           that exists but is not yet visible to the counter. */
        atomic_xadd(&s->workers_live, 1);
        if (spawn_thread(worker_main, s) != 0) {
            atomic_xadd(&s->workers_live, -1);
            return;
        }
    }
}

static void publish(Scanner *s, Worker *w, const DirRef *d) {
    atomic_xadd(&s->active, 1);
    if (q_push(s, d)) {
        atomic_xadd(&s->work_gen, 1);
        maybe_spawn(s);
        if (atomic_load(&s->idle) > 0) wake_all(s);
    } else if (!lstk_push(w, d)) {
        atomic_xadd(&s->active, -1);   /* arena exhausted */
        atomic_or(&s->err, ERR_ARENA);
    }
}

static void flag_err(Scanner *s, i64 bit) {
    if (!(atomic_load(&s->err) & bit)) atomic_or(&s->err, bit);
}

/* Under the output lock so it cannot land inside a result line or the bar. */
static void warn_path(Scanner *s, const char *what, const char *path) {
    flag_err(s, ERR_OPENDIR);
    if (s->cfg->quiet_errs) return;
    out_lock();
    if (s->cfg->bar) display_clear();
    write_str(STDERR_FILENO, "crom: ");
    write_str(STDERR_FILENO, what);
    write_str(STDERR_FILENO, " '");
    write_str(STDERR_FILENO, path);
    write_str(STDERR_FILENO, "'\n");
    out_unlock();
}

/* Some filesystems (FUSE, NFS, overlayfs) always answer DT_UNKNOWN, and taking
   that as "neither file nor directory" made crom find nothing there. */
static u8 resolve_type(i64 dirfd, const char *name) {
    struct stat64 st;
    if (syscall4(SYS_newfstatat, dirfd, (long)name, (long)&st,
                 AT_SYMLINK_NOFOLLOW) < 0) return DT_UNKNOWN;
    switch (st.st_mode & S_IFMT) {
        case S_IFDIR:  return DT_DIR;
        case S_IFREG:  return DT_REG;
        case S_IFLNK:  return DT_LNK;
        case S_IFIFO:  return DT_FIFO;
        case S_IFSOCK: return DT_SOCK;
        case S_IFCHR:  return DT_CHR;
        case S_IFBLK:  return DT_BLK;
    }
    return DT_UNKNOWN;
}

/* Type of what a symlink points at, plus its identity for the loop check. */
static u8 resolve_link(i64 dirfd, const char *name, u64 *dev, u64 *ino) {
    struct stat64 st;
    if (syscall4(SYS_newfstatat, dirfd, (long)name, (long)&st, 0) < 0)
        return DT_LNK;
    *dev = st.st_dev; *ino = st.st_ino;
    switch (st.st_mode & S_IFMT) {
        case S_IFDIR: return DT_DIR;
        case S_IFREG: return DT_REG;
    }
    return DT_UNKNOWN;
}

static i64 is_ancestor(const LinkNode *chain, u64 dev, u64 ino) {
    for (; chain; chain = chain->parent)
        if (chain->ino == ino && chain->dev == dev) return 1;
    return 0;
}

static void buf_add(char *buf, i64 *olen, const char *p, i64 plen) {
    for (i64 i = 0; i < plen; i++) buf[(*olen)++] = p[i];
}

static void buf_flush(Worker *w) {
    if (w->olen <= 0) return;
    out_lock();
    if (w->s->cfg->bar) display_clear();
    write_all(STDOUT_FILENO, w->obuf, w->olen);
    out_unlock();
    w->olen = 0;
}

static void buf_emit(Worker *w, const char *path, i64 len, i64 json) {
    const char term = w->s->cfg->null_sep ? '\0' : '\n';

    if (json) {
        /* Six bytes per byte is the worst case (\u00XX). */
        if (w->olen + 6 * len + 16 > OBUF_SZ) buf_flush(w);
        buf_add(w->obuf, &w->olen, "{\"path\":\"", 9);
        for (i64 i = 0; i < len; i++) {
            u8 ch = (u8)path[i];
            switch (ch) {
                case '"':  buf_add(w->obuf, &w->olen, "\\\"", 2); continue;
                case '\\': buf_add(w->obuf, &w->olen, "\\\\", 2); continue;
                case '\b': buf_add(w->obuf, &w->olen, "\\b", 2);  continue;
                case '\f': buf_add(w->obuf, &w->olen, "\\f", 2);  continue;
                case '\n': buf_add(w->obuf, &w->olen, "\\n", 2);  continue;
                case '\r': buf_add(w->obuf, &w->olen, "\\r", 2);  continue;
                case '\t': buf_add(w->obuf, &w->olen, "\\t", 2);  continue;
            }
            if (ch < 0x20) {
                static const char hex[] = "0123456789abcdef";
                buf_add(w->obuf, &w->olen, "\\u00", 4);
                w->obuf[w->olen++] = hex[ch >> 4];
                w->obuf[w->olen++] = hex[ch & 15];
                continue;
            }
            w->obuf[w->olen++] = (char)ch;
        }
        buf_add(w->obuf, &w->olen, "\"}", 2);
        w->obuf[w->olen++] = term;
    } else {
        if (w->olen + len + 1 > OBUF_SZ) buf_flush(w);
        buf_add(w->obuf, &w->olen, path, len);
        w->obuf[w->olen++] = term;
    }

    if (w->s->cfg->tty_out) buf_flush(w);
}

/* {} expands to a single-quoted path so a name like `a; rm -rf x` stays inert;
   an overflow refuses to run rather than executing half a command. */
static i64 exec_quote(char *buf, i64 cap, const char *cmd,
                      const char *path, i64 len) {
    i64 pos = 0;
    for (const char *c = cmd; *c; ) {
        if (c[0] == '{' && c[1] == '}') {
            if (pos + 2 > cap) return -1;
            buf[pos++] = '\'';
            for (i64 i = 0; i < len; i++) {
                if (path[i] == '\'') {
                    if (pos + 4 > cap) return -1;
                    buf[pos++] = '\''; buf[pos++] = '\\';
                    buf[pos++] = '\''; buf[pos++] = '\'';
                } else {
                    if (pos + 1 > cap) return -1;
                    buf[pos++] = path[i];
                }
            }
            if (pos + 1 > cap) return -1;
            buf[pos++] = '\'';
            c += 2;
        } else {
            if (pos + 1 > cap) return -1;
            buf[pos++] = *c++;
        }
    }
    if (pos >= cap) return -1;
    buf[pos] = 0;
    return pos;
}

static void exec_file(Scanner *s, const char *cmd, const char *path, i64 len) {
    char buf[PBUF_SZ];
    if (exec_quote(buf, (i64)sizeof(buf) - 1, cmd, path, len) < 0) {
        flag_err(s, ERR_EXEC);
        return;
    }

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
                     i64 len, const char *name, i64 nlen, u8 type) {
    const ScanCfg *c = s->cfg;

    if (c->type_filter) {
        if (c->type_filter == 'f' && type != DT_REG) return;
        if (c->type_filter == 'd' && type != DT_DIR) return;
        if (c->type_filter == 'l' && type != DT_LNK) return;
    } else if (type != DT_REG) {
        return;
    }

    if (c->pattern && !name_match(c->nm, name, nlen)) return;
    if (c->size_cmp && !size_pass(c, dirfd, name)) return;
    if (c->needle &&
        !search_file_at(dirfd, name, c->needle, c->needle_len,
                        w->rbuf, RBUF_SZ)) return;

    if (c->max_results) {
        if (atomic_xadd(&s->emitted, 1) >= c->max_results) { scan_finish(s); return; }
    }

    w->c_matches++;

    if (c->exec_cmd) {
        out_lock();
        exec_file(s, c->exec_cmd, path, len);
        out_unlock();
    } else {
        buf_emit(w, path, len, c->json_out);
    }

    if (c->max_results && atomic_load(&s->emitted) >= c->max_results)
        scan_finish(s);
}

static void scan_dir(Scanner *s, Worker *w, const DirRef *d) {
    i64 fd = syscall3(SYS_openat, AT_FDCWD, (long)d->path,
                      O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (fd < 0) { warn_path(s, "cannot open", d->path); return; }

    const ScanCfg *c = s->cfg;
    const GitNode *ign = d->ign;
    const LinkNode *chain = d->anc;
    if (c->follow) {
        struct stat64 st;
        LinkNode *node = (LinkNode *)bump_alloc(&s->arena, sizeof(LinkNode));
        if (node && syscall2(SYS_fstat, fd, (long)&st) >= 0) {
            node->parent = chain; node->dev = st.st_dev; node->ino = st.st_ino;
            chain = node;
        }
    }
    if (c->use_ignore)
        ign = gitignore_load(&s->arena, ign, d->path, d->len);

    char pbuf[PBUF_SZ];
    i64 plen = d->len;
    if (plen >= PBUF_SZ - 2) {
        flag_err(s, ERR_TOOLONG);
        syscall1(SYS_close, fd);
        return;
    }
    for (i64 i = 0; i < plen; i++) pbuf[i] = d->path[i];
    if (plen == 0) pbuf[plen++] = '.';
    if (pbuf[plen - 1] != '/') pbuf[plen++] = '/';
    i64 prefix = plen;

    u8 dbuf[DBUF_SZ];
    for (;;) {
        i64 n = syscall3(SYS_getdents64, fd, (long)dbuf, DBUF_SZ);
        if (n <= 0) break;
        if (c->max_results && atomic_load(&s->done)) break;

        for (i64 pos = 0; pos < n; ) {
            struct linux_dirent64 *e = (struct linux_dirent64 *)(dbuf + pos);
            pos += e->d_reclen;

            const char *name = e->d_name;
            if (name[0] == '.') {
                if (name[1] == 0) continue;
                if (name[1] == '.' && name[2] == 0) continue;
                /* Dot entries only on request: otherwise a search under $HOME
                   spends its time in ~/.cache. */
                if (!c->hidden) continue;
            }

            u8 type = e->d_type;
#ifdef CROM_FORCE_DT_UNKNOWN
            type = DT_UNKNOWN;      /* test build: pretend the fs told us nothing */
#endif
            if (type == DT_UNKNOWN) type = resolve_type(fd, name);

            if (c->follow && type == DT_LNK) {
                u64 dev = 0, ino = 0;
                u8 t = resolve_link(fd, name, &dev, &ino);
                if (t == DT_DIR && is_ancestor(chain, dev, ino)) continue;
                type = t;
            }

            i32 is_dir = (type == DT_DIR);
            if (is_dir && match_ignore(name, 1)) continue;
            if (c->use_ignore && gitignore_check(ign, name, is_dir)) continue;

            i64 nl = str_len(name);
            /* Prunes directories too, so -E node_modules is never walked. */
            if (c->n_ex) {
                i64 skip = 0;
                for (i64 k = 0; k < c->n_ex; k++)
                    if (name_match(&c->ex[k], name, nl)) { skip = 1; break; }
                if (skip) continue;
            }
            if (prefix + nl >= PBUF_SZ) { flag_err(s, ERR_TOOLONG); continue; }
            for (i64 i = 0; i < nl; i++) pbuf[prefix + i] = name[i];
            i64 flen = prefix + nl;
            pbuf[flen] = 0;

            if (is_dir) {
                w->c_dirs++;

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
                        sub.anc = chain;
                        publish(s, w, &sub);
                    } else {
                        flag_err(s, ERR_ARENA);
                    }
                }
            } else if (type == DT_REG) {
                w->c_files++;
                if ((w->c_files & 511) == 0) wc_publish(w);
            }

            consider(s, w, fd, pbuf, flen, name, nl, type);
        }
    }

    syscall1(SYS_close, fd);
}

static void worker_loop(Scanner *s, Worker *w) {
    for (;;) {
        DirRef d;

        if (atomic_load(&s->done)) break;

        if (lstk_pop(w, &d) || q_pop(s, &d)) {
            scan_dir(s, w, &d);
            wc_publish(w);
            if (atomic_xadd(&s->active, -1) == 1) scan_finish(s);
            continue;
        }

        if (atomic_load(&s->done)) break;

        atomic_xadd(&s->idle, 1);
        i64 gen = atomic_load(&s->work_gen);
        if (q_pop(s, &d)) {
            atomic_xadd(&s->idle, -1);
            scan_dir(s, w, &d);
            wc_publish(w);
            if (atomic_xadd(&s->active, -1) == 1) scan_finish(s);
            continue;
        }
        if (!atomic_load(&s->done) && atomic_load(&s->work_gen) == gen)
            syscall6(SYS_futex, (long)&s->work_gen,
                     FUTEX_WAIT|FUTEX_PRIVATE_FLAG, (i32)gen, 0, 0, 0);
        atomic_xadd(&s->idle, -1);
    }
}

static void worker_init(Worker *w, Scanner *s, i64 id) {
    w->s = s; w->top = 0; w->freelist = 0; w->ln = 0; w->olen = 0;
    w->wc = &s->wc[id];
    w->c_dirs = 0; w->c_files = 0; w->c_matches = 0;
}


static i64 worker_main(void *arg) {
    Scanner *s = (Scanner *)arg;
    Worker w;
    worker_init(&w, s, atomic_xadd(&s->next_id, 1));

    worker_loop(s, &w);
    wc_publish(&w);
    buf_flush(&w);

    atomic_xadd(&s->workers_exited, 1);
    syscall6(SYS_futex, (long)&s->workers_exited,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);
    return 0;
}

static i64 bar_main(void *arg) {
    Scanner *s = (Scanner *)arg;
    i64 delay_ns = 250000000L;

    for (;;) {
        struct { i64 sec; i64 nsec; } ts;
        ts.sec  = delay_ns / 1000000000L;
        ts.nsec = delay_ns % 1000000000L;
        syscall6(SYS_futex, (long)&s->done,
                 FUTEX_WAIT|FUTEX_PRIVATE_FLAG, 0, (long)&ts, 0, 0);

        if (atomic_load(&s->done)) break;
        delay_ns = 66000000L;

        i64 d = 0, f = 0, m = 0;
        for (i64 i = 0; i < s->cfg->num_workers; i++) {
            d += s->wc[i].dirs; f += s->wc[i].files; m += s->wc[i].matches;
        }
        out_lock();
        display_update(d, f, m);
        out_unlock();
    }

    out_lock();
    display_clear();
    out_unlock();

    atomic_xadd(&s->bar_exited, 1);
    syscall6(SYS_futex, (long)&s->bar_exited,
             FUTEX_WAKE|FUTEX_PRIVATE_FLAG, 2147483647, 0, 0, 0);
    return 0;
}

static i64 spawn_thread(i64 (*fn)(void *), void *arg) {
    i64 stksz = 1048576;
    void *stk = (void *)syscall6(SYS_mmap, 0, (unsigned long)stksz,
                                 PROT_READ|PROT_WRITE,
                                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (is_mmap_err(stk)) return -1;

    u64 sp = (u64)stk + stksz;
    sp &= ~15ULL;
    sp -= 24;
    ((void **)(u64)sp)[0] = (void *)clone_trampoline;
    ((void **)(u64)sp)[1] = arg;
    ((void **)(u64)sp)[2] = (void *)fn;

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

i64 scan_run(Scanner *s, const ScanCfg *cfg, const char **roots, i64 nroots) {
    s->cfg = cfg;
    s->head = 0; s->tail = 0;
    s->active = 0; s->done = 0; s->work_gen = 0; s->idle = 0;
    s->n_dirs = 0; s->n_files = 0; s->n_matches = 0; s->emitted = 0;
    s->bar_live = 0; s->bar_exited = 0; s->err = 0;
    s->next_id = 1;   /* slot 0 belongs to the calling thread */
    s->spawned = 0;
    /* Only the slots this run can use; all 256 meant 16 KB of dead pages. */
    for (i64 i = 0; i < cfg->num_workers; i++) {
        s->wc[i].dirs = 0; s->wc[i].files = 0; s->wc[i].matches = 0;
    }
    s->workers_live = 0; s->workers_exited = 0;
    g_out_lock = 0;

    if (bump_init(&s->arena, ARENA_CAP) < 0) { s->err = ERR_FATAL; return 0; }

    /* All roots go in before any worker starts: own depth origin for each. */
    i64 seeded = 0;
    for (i64 k = 0; k < nroots; k++) {
        const char *root = roots[k];
        if (!root || !root[0]) root = ".";
        i64 rl = str_len(root);
        char *rp = (char *)bump_alloc(&s->arena, rl + 1);
        if (!rp) { s->err = ERR_FATAL; return 0; }
        for (i64 i = 0; i <= rl; i++) rp[i] = root[i];

        DirRef r;
        r.path = rp; r.len = (i32)rl; r.depth = 0; r.ign = 0; r.anc = 0;
        atomic_xadd(&s->active, 1);
        if (!q_push(s, &r)) { atomic_xadd(&s->active, -1); break; }
        seeded++;
    }
    if (seeded == 0) { s->err = ERR_FATAL; return 0; }

    if (cfg->bar && spawn_thread(bar_main, s) == 0)
        atomic_xadd(&s->bar_live, 1);

    Worker w;
    worker_init(&w, s, 0);
    worker_loop(s, &w);
    wc_publish(&w);
    buf_flush(&w);

    /* Re-read the count each turn: workers are spawned lazily, so one may
       appear after we start waiting. */
    while (atomic_load(&s->workers_exited) < atomic_load(&s->workers_live)) {
        scan_finish(s);
        syscall1(SYS_sched_yield, 0);
    }

    /* Bar thread must be gone before main draws the summary line. */
    while (atomic_load(&s->bar_exited) < atomic_load(&s->bar_live))
        syscall1(SYS_sched_yield, 0);

    for (i64 i = 0; i < cfg->num_workers; i++) {
        s->n_dirs += s->wc[i].dirs;
        s->n_files += s->wc[i].files;
        s->n_matches += s->wc[i].matches;
    }
    return s->n_matches;
}
