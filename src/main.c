#include "syscalls.h"
#include "types.h"
#include "util.h"
#include "walk.h"
#include "match_name.h"
#include "pool.h"
#include "display.h"
#include "ignore.h"
#include "match_content.h"

static const char *prog = "crom";
static const char *pattern;
static const char *needle;
static i64 needle_len;
static i64 num_threads;
static Scanner scanner;
static i64 g_matches;
static i64 g_bar;
static i64 g_use_ignore = 1;
static int  g_type_filter;  /* 0=any, 'f','d','l' */
static i64 g_size_cmp;     /* -1=lt, 0=no filter, 1=gt */
static i64 g_size_val;
static i64 g_max_depth = -1;
static i64 g_json;
static i64 g_color = 1;
static i64 g_binary;   /* -a: search binary files too */
static const char *g_exec_cmd;

static i64 is_tty(i64 fd) {
    struct stat64 st;
    if (syscall2(SYS_fstat, fd, (long)&st) < 0) return 0;
    return (st.st_mode & S_IFMT) == S_IFCHR;
}

static void usage(void) {
    write_str(STDOUT_FILENO, "\033[1;30m\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\033[0m\n");
    write_str(STDOUT_FILENO, "\033[1;30m\xe2\x94\x82\033[0m  \033[1;36mcrom\033[0m \xe2\x80\x94 the fast file hunter                \033[1;30m\xe2\x94\x82\033[0m\n");
    write_str(STDOUT_FILENO, "\033[1;30m\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\033[0m\n\n");
    write_str(STDOUT_FILENO, "\033[1mUsage:\033[0m ");
    write_str(STDOUT_FILENO, prog);
    write_str(STDOUT_FILENO, " [options] [pattern] [path...]\n\n");
    write_str(STDOUT_FILENO, "\033[1mOptions:\033[0m\n");
    write_str(STDOUT_FILENO, "  \033[33m-n\033[0m, \033[33m--name\033[0m <glob>       Match filename pattern\n");
    write_str(STDOUT_FILENO, "  \033[33m-c\033[0m, \033[33m--content\033[0m <text>    Search file contents\n");
    write_str(STDOUT_FILENO, "  \033[33m-t\033[0m, \033[33m--type\033[0m <f|d|l>     Filter by type\n");
    write_str(STDOUT_FILENO, "  \033[33m-s\033[0m, \033[33m--size\033[0m <[+-]N>     Filter by size\n");
    write_str(STDOUT_FILENO, "  \033[33m--depth\033[0m <N>           Max recursion depth\n");
    write_str(STDOUT_FILENO, "  \033[33m-e\033[0m, \033[33m--exec\033[0m <cmd> {}    Execute command per result\n");
    write_str(STDOUT_FILENO, "  \033[33m-j\033[0m, \033[33m--threads\033[0m <N>     Worker threads\n");
    write_str(STDOUT_FILENO, "  \033[33m--color\033[0m <when>        auto|always|never\n");
    write_str(STDOUT_FILENO, "  \033[33m-a\033[0m, \033[33m--text\033[0m            Search binary files too\n");
    write_str(STDOUT_FILENO, "  \033[33m--no-ignore\033[0m           Ignore .gitignore rules\n");
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

static i64 parse_int(const char *s) {
    i64 v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v;
}

static i64 parse_size(const char *s) {
    if (*s == '+' || *s == '-') {
        g_size_cmp = (*s == '+') ? 1 : -1;
        s++;
    }
    i64 v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    if (*s == 'k' || *s == 'K') { v *= 1024; s++; }
    else if (*s == 'm' || *s == 'M') { v *= 1048576; s++; }
    else if (*s == 'g' || *s == 'G') { v *= 1073741824; s++; }
    return v;
}

static i64 nproc(void) {
    unsigned long mask[16];
    long n = syscall3(SYS_sched_getaffinity, 0, sizeof(mask), (long)mask);
    if (n <= 0) return 1;
    i64 count = 0;
    i64 words = n / (i64)sizeof(unsigned long);
    if (words > 16) words = 16;
    for (i64 i = 0; i < words; i++) {
        unsigned long v = mask[i];
        while (v) { count++; v &= v - 1; }
    }
    return count > 0 ? count : 1;
}

int crom_main(int argc, char **argv) {
    if (argv[0]) {
        const char *p = argv[0];
        i64 plen = str_len(p);
        for (i64 i = plen - 1; i >= 0 && p[i] != '/'; i--) {
            if (i == 0 || p[i-1] == '/') {
                prog = p + i;
                break;
            }
        }
    }

    if (argc < 2) {
        usage();
        return 0;
    }

    const char *target = 0;

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-h") || str_eq(argv[i], "--help")) {
            usage();
            return 0;
        }
        if (str_eq(argv[i], "-V") || str_eq(argv[i], "--version")) {
            write_str(STDOUT_FILENO, "crom 0.1.0\n");
            return 0;
        }
        if (str_eq(argv[i], "-n") || str_eq(argv[i], "--name")) {
            if (i + 1 < argc) pattern = argv[++i];
            continue;
        }
        if (str_eq(argv[i], "-c") || str_eq(argv[i], "--content")) {
            if (i + 1 < argc) {
                needle = argv[++i];
                needle_len = str_len(needle);
            }
            continue;
        }
        if (str_eq(argv[i], "-j") || str_eq(argv[i], "--threads")) {
            if (i + 1 < argc) num_threads = parse_int(argv[++i]);
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] == 'j' && argv[i][2]) {
            num_threads = parse_int(argv[i] + 2);
            continue;
        }
        if (str_eq(argv[i], "-t") || str_eq(argv[i], "--type")) {
            if (i + 1 < argc) g_type_filter = argv[++i][0];
            continue;
        }
        if (str_eq(argv[i], "-s") || str_eq(argv[i], "--size")) {
            if (i + 1 < argc) g_size_val = parse_size(argv[++i]);
            continue;
        }
        if (str_eq(argv[i], "--depth")) {
            if (i + 1 < argc) g_max_depth = parse_int(argv[++i]);
            continue;
        }
        if (str_eq(argv[i], "--bar")) {
            g_bar = 1;
            continue;
        }
        if (str_eq(argv[i], "-a") || str_eq(argv[i], "--text")) {
            g_binary = 1;
            continue;
        }
        if (str_eq(argv[i], "--no-ignore")) {
            g_use_ignore = 0;
            continue;
        }
        if (str_eq(argv[i], "--json")) {
            g_json = 1;
            continue;
        }
        if (str_eq(argv[i], "--color")) {
            if (i + 1 < argc) {
                if (str_eq(argv[++i], "never")) g_color = 0;
                else if (str_eq(argv[i], "always")) g_color = 2;
                else g_color = 1;
            }
            continue;
        }
        if (str_eq(argv[i], "--no-color")) {
            g_color = 0;
            continue;
        }
        if (str_eq(argv[i], "-e") || str_eq(argv[i], "--exec")) {
            if (i + 1 < argc) g_exec_cmd = argv[++i];
            continue;
        }
        if (argv[i][0] != '-') {
            if (argv[i][0] == '/' || argv[i][0] == '.' || argv[i][0] == '~') {
                if (!target) target = argv[i];
            } else {
                if (!pattern) pattern = argv[i];
            }
        }
    }

    if (!target) target = ".";
    if (!pattern && !needle) pattern = "*";
    if (num_threads <= 0) num_threads = nproc();
    if (g_size_cmp == 0 && g_size_val > 0) g_size_cmp = 1;

    if (g_bar) display_init();
    ignore_set_enabled(g_use_ignore);
    if (needle) content_prepare(needle, needle_len);
    content_set_text_only(!g_binary);

    ScanCfg cfg;
    cfg.pattern     = pattern;
    cfg.needle      = needle;
    cfg.needle_len  = needle_len;
    cfg.exec_cmd    = g_exec_cmd;
    cfg.type_filter = g_type_filter;
    cfg.size_cmp    = g_size_cmp;
    cfg.size_val    = g_size_val;
    cfg.max_depth   = g_max_depth;
    cfg.json_out    = g_json;
    cfg.use_ignore  = g_use_ignore;
    cfg.bar         = g_bar;
    cfg.num_workers = num_threads;

    g_matches = scan_run(&scanner, &cfg, target);

    if (g_bar)
        display_done(scanner.n_dirs, scanner.n_files, g_matches, 0);

    return 0;
}
