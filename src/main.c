#include "syscalls.h"

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef int i32;
typedef unsigned char u8;

typedef struct {
    void *buf;
    i64 cap;
    i64 len;
    i64 pos;
} Arena;

static void *arena_alloc(Arena *a, i64 sz) {
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

static void arena_free(Arena *a) {
    if (a->buf && a->cap) syscall2(SYS_munmap, (long)a->buf, a->cap);
    a->buf = 0; a->cap = 0; a->len = 0; a->pos = 0;
}

static i64 str_len(const char *s) {
    const char *p = s;
    while (*p) p++;
    return p - s;
}

static i64 str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static i64 str_eqn(const char *a, const char *b, i64 n) {
    for (i64 i = 0; i < n; i++) {
        if (!a[i] || !b[i]) return 0;
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static char *str_dup(Arena *a, const char *s) {
    i64 n = str_len(s);
    char *d = arena_alloc(a, n + 1);
    if (!d) return 0;
    for (i64 i = 0; i <= n; i++) d[i] = s[i];
    return d;
}

static i64 write_all(i64 fd, const char *s, i64 len) {
    i64 off = 0;
    while (off < len) {
        i64 n = syscall3(SYS_write, fd, (long)(s + off), (unsigned long)(len - off));
        if (n < 0) return -1;
        off += n;
    }
    return off;
}

static i64 write_str(i64 fd, const char *s) {
    return write_all(fd, s, str_len(s));
}

static void usage(const char *prog) {
    write_str(STDOUT_FILENO, "\033[1;30m┌───────────────────────────────────────────┐\033[0m\n");
    write_str(STDOUT_FILENO, "\033[1;30m│\033[0m  \033[1;36mcrom\033[0m — the fast file hunter                \033[1;30m│\033[0m\n");
    write_str(STDOUT_FILENO, "\033[1;30m└───────────────────────────────────────────┘\033[0m\n\n");
    write_str(STDOUT_FILENO, "\033[1mUsage:\033[0m ");
    write_str(STDOUT_FILENO, prog);
    write_str(STDOUT_FILENO, " [options] [pattern] [path...]\n\n");
    write_str(STDOUT_FILENO, "\033[1mOptions:\033[0m\n");
    write_str(STDOUT_FILENO, "  \033[33m-n\033[0m, \033[33m--name\033[0m <glob>       Match filename pattern\n");
    write_str(STDOUT_FILENO, "  \033[33m-c\033[0m, \033[33m--content\033[0m <text>    Search file contents\n");
    write_str(STDOUT_FILENO, "  \033[33m-t\033[0m, \033[33m--type\033[0m <f|d|l>     Filter by type\n");
    write_str(STDOUT_FILENO, "  \033[33m-s\033[0m, \033[33m--size\033[0m <N>         Filter by size (+/- prefix)\n");
    write_str(STDOUT_FILENO, "  \033[33m--since\033[0m <N>[d|h|m]     Modified within range\n");
    write_str(STDOUT_FILENO, "  \033[33m--depth\033[0m <N>           Max recursion depth\n");
    write_str(STDOUT_FILENO, "  \033[33m-e\033[0m, \033[33m--exec\033[0m <cmd> {}    Execute command per result\n");
    write_str(STDOUT_FILENO, "  \033[33m-j\033[0m, \033[33m--threads\033[0m <N>     Worker threads\n");
    write_str(STDOUT_FILENO, "  \033[33m--color\033[0m <when>        auto|always|never\n");
    write_str(STDOUT_FILENO, "  \033[33m--json\033[0m               JSON output\n");
    write_str(STDOUT_FILENO, "  \033[33m--bar\033[0m                Progress bar + spinner\n");
    write_str(STDOUT_FILENO, "  \033[33m-h\033[0m, \033[33m--help\033[0m            Show help\n");
    write_str(STDOUT_FILENO, "  \033[33m-V\033[0m, \033[33m--version\033[0m         Print version\n\n");
    write_str(STDOUT_FILENO, "\033[1mExamples:\033[0m\n");
    write_str(STDOUT_FILENO, "  crom '*.c'                    All .c files\n");
    write_str(STDOUT_FILENO, "  crom --content 'TODO' .       Files with TODO\n");
    write_str(STDOUT_FILENO, "  crom -n '*.rs' -s +1m         Rust files > 1MB\n");
    write_str(STDOUT_FILENO, "  crom -t f -c 'fn main' src/   Files with 'fn main'\n");
    write_str(STDOUT_FILENO, "  crom '**/test*' --bar         Test files, with progress\n");
}

int crom_main(int argc, char **argv) {
    const char *prog = argv[0];
    if (prog && (str_eq(prog + str_len(prog) - 5, "/crom") ||
                 str_eq(prog, "crom") ||
                 (str_len(prog) >= 4 && str_eq(prog + str_len(prog) - 4, "crom")))) {
        prog = "crom";
    }

    if (argc < 2) {
        usage(prog);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-h") || str_eq(argv[i], "--help")) {
            usage(prog);
            return 0;
        }
        if (str_eq(argv[i], "-V") || str_eq(argv[i], "--version")) {
            write_str(STDOUT_FILENO, "crom 0.1.0\n");
            return 0;
        }
    }

    write_str(STDOUT_FILENO, "crom: not yet implemented\n");
    return 1;
}


