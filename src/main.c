#include "syscalls.h"
#include "types.h"
#include "util.h"
#include "walk.h"
#include "match_name.h"
#include "match_content.h"
#include "pool.h"
#include "display.h"
#include "ignore.h"

static const char *prog = "crom";
static const char *pattern;
static const char *needle;
static i64 needle_len;
static i64 num_threads;
static Scanner scanner;
static volatile i64 g_matches;
static i64 g_bar;
static i64 g_use_ignore = 1;
static int  g_type_filter;
static i64 g_size_cmp;
static i64 g_size_val;
static i64 g_max_depth = -1;
static i64 g_json;
static i64 g_null_sep;
static i64 g_want_help;
static i64 g_want_version;
static i64 g_color = 1;          /* 0 never, 1 auto, 2 always */

/* Resolved per stream: help goes to stdout, the bar and summary to stderr,
   and either can be a terminal while the other is a pipe. */
static i64 color_on(i64 fd) {
    if (g_color == 0) return 0;
    if (g_color >= 2) return 1;
    return is_tty(fd);
}
static i64 g_binary;
static const char *g_exec_cmd;

static const char *target;
static i32 saw_dash;

static void usage(void) {
    i64 c = color_on(STDOUT_FILENO);
    write_str_c(STDOUT_FILENO, "\033[1;36mcrom\033[0m — fast file hunter\n\n", c);
    write_str_c(STDOUT_FILENO, "  \033[1mcrom\033[0m [pattern] [path]\n\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-n\033[0m, \033[33m--name\033[0m <glob>     filename pattern\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-c\033[0m, \033[33m--content\033[0m <t>    search contents\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-t\033[0m, \033[33m--type\033[0m <f|d|l>   filter by type\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-s\033[0m, \033[33m--size\033[0m <[+-]N>   filter by size\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m--depth\033[0m <N>         max recursion depth\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-e\033[0m, \033[33m--exec\033[0m <cmd> {}  run command per result\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-j\033[0m, \033[33m--threads\033[0m <N>   worker threads\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-a\033[0m, \033[33m--text\033[0m          search binary files\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-0\033[0m, \033[33m--null\033[0m          null-separated output\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m--json\033[0m             JSON output\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m--bar\033[0m              progress bar\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m--no-ignore\033[0m         skip .gitignore\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m--no-config\033[0m         skip config file\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m--color\033[0m <when>      auto|always|never\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-h\033[0m, \033[33m--help\033[0m          show help\n", c);
    write_str_c(STDOUT_FILENO, "  \033[33m-V\033[0m, \033[33m--version\033[0m       print version\n", c);
    write_str_c(STDOUT_FILENO, "\n  \033[1mexit\033[0m  \033[32m0\033[0m found  \033[32m1\033[0m none  \033[32m2\033[0m error\n", c);
}

static i64 parse_int(const char *s) {
    i64 v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v;
}

static i64 parse_size(const char *s) {
    if (*s == '+' || *s == '-') { g_size_cmp = (*s == '+') ? 1 : -1; s++; }
    i64 v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    if (*s == 'k' || *s == 'K') v *= 1024;
    else if (*s == 'm' || *s == 'M') v *= 1048576;
    else if (*s == 'g' || *s == 'G') v *= 1073741824;
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

static char *getenv_c(const char *name, char **envp) {
    i64 nl = str_len(name);
    while (*envp) {
        const char *e = *envp;
        i64 i;
        for (i = 0; i < nl && e[i] == name[i]; i++);
        if (i == nl && e[nl] == '=') return (char *)(e + nl + 1);
        envp++;
    }
    return 0;
}

#define CFG_BUF_SZ 2048
static char cfg_buf[CFG_BUF_SZ];
static char *cfg_argv[128];
static i64 cfg_argc;

static i64 load_config(char **envp) {
    char *home = getenv_c("HOME", envp);
    if (!home) return 0;

    char path[1024];
    i64 pl = 0, hl = str_len(home);
    for (i64 i = 0; i < hl; i++) path[pl++] = home[i];
    path[pl++] = '/'; path[pl++] = '.'; path[pl++] = 'c';
    path[pl++] = 'r'; path[pl++] = 'o'; path[pl++] = 'm';
    path[pl++] = 'r'; path[pl++] = 'c';
    path[pl] = 0;

    i32 fd = (i32)syscall3(SYS_openat, AT_FDCWD, (long)path, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 0;

    i64 n = syscall3(SYS_read, fd, (long)cfg_buf, CFG_BUF_SZ - 1);
    syscall1(SYS_close, fd);
    if (n <= 0) return 0;
    cfg_buf[n] = 0;

    cfg_argc = 0;
    char *p = cfg_buf;
    while (*p && cfg_argc < 127) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (*p == '#' || *p == 0) { while (*p && *p != '\n') p++; continue; }
        cfg_argv[cfg_argc++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if (*p) { *p = 0; p++; }
    }
    cfg_argv[cfg_argc] = 0;
    return cfg_argc;
}

static void parse_arg(const char *arg, int ac, char **av, int *pi) {
    int i = *pi;

    if (!saw_dash && str_eq(arg, "--")) { saw_dash = 1; *pi = i; return; }
    if (saw_dash) {
        if (!pattern) pattern = arg;
        else if (!target) target = arg;
        *pi = i; return;
    }

    if (str_eq(arg, "-h") || str_eq(arg, "--help")) { g_want_help = 1; *pi = i; return; }
    if (str_eq(arg, "-V") || str_eq(arg, "--version")) {
        g_want_version = 1; *pi = i; return;
    }
    if (str_eq(arg, "-n") || str_eq(arg, "--name")) {
        if (i + 1 < ac) pattern = av[++i]; *pi = i; return;
    }
    if (str_eq(arg, "-c") || str_eq(arg, "--content")) {
        if (i + 1 < ac) { needle = av[++i]; needle_len = str_len(needle); }
        *pi = i; return;
    }
    if (str_eq(arg, "-j") || str_eq(arg, "--threads")) {
        if (i + 1 < ac) num_threads = parse_int(av[++i]); *pi = i; return;
    }
    if (arg[0] == '-' && arg[1] == 'j' && arg[2]) {
        num_threads = parse_int(arg + 2); *pi = i; return;
    }
    if (str_eq(arg, "-t") || str_eq(arg, "--type")) {
        if (i + 1 < ac) g_type_filter = av[++i][0]; *pi = i; return;
    }
    if (str_eq(arg, "-s") || str_eq(arg, "--size")) {
        if (i + 1 < ac) g_size_val = parse_size(av[++i]); *pi = i; return;
    }
    if (str_eq(arg, "--depth")) {
        if (i + 1 < ac) g_max_depth = parse_int(av[++i]); *pi = i; return;
    }
    if (str_eq(arg, "--bar")) { g_bar = 1; *pi = i; return; }
    if (str_eq(arg, "-a") || str_eq(arg, "--text")) { g_binary = 1; *pi = i; return; }
    if (str_eq(arg, "-0") || str_eq(arg, "--null")) { g_null_sep = 1; *pi = i; return; }
    if (str_eq(arg, "--no-ignore")) { g_use_ignore = 0; *pi = i; return; }
    if (str_eq(arg, "--no-config")) { *pi = i; return; }
    if (str_eq(arg, "--json")) { g_json = 1; *pi = i; return; }
    if (str_eq(arg, "--no-color")) { g_color = 0; *pi = i; return; }
    if (str_eq(arg, "--color")) {
        if (i + 1 < ac) {
            if (str_eq(av[++i], "never")) g_color = 0;
            else if (str_eq(av[i], "always")) g_color = 2;
            else g_color = 1;
        }
        *pi = i; return;
    }
    if (str_eq(arg, "-e") || str_eq(arg, "--exec")) {
        if (i + 1 < ac) g_exec_cmd = av[++i]; *pi = i; return;
    }
    if (arg[0] != '-') {
        if (arg[0] == '/' || arg[0] == '.' || arg[0] == '~') {
            if (!target) target = arg;
        } else {
            if (!pattern) pattern = arg;
        }
    }
    *pi = i;
}

int crom_main(int argc, char **argv, char **envp) {
    if (argv[0]) {
        const char *p = argv[0];
        i64 plen = str_len(p);
        for (i64 i = plen - 1; i >= 0 && p[i] != '/'; i--)
            if (i == 0 || p[i-1] == '/') { prog = p + i; break; }
    }

    i32 no_cfg = 0;
    for (int i = 1; i < argc; i++)
        if (str_eq(argv[i], "--no-config")) no_cfg = 1;

    target = 0;
    saw_dash = 0;

    if (!no_cfg) {
        i64 ca = load_config(envp);
        if (ca > 0)
            for (int i = 0; i < (int)ca; i++) parse_arg(cfg_argv[i], (int)ca, cfg_argv, &i);
    }

    for (int i = 1; i < argc; i++)
        parse_arg(argv[i], argc, argv, &i);

    if (g_want_help)    { usage(); return 0; }
    if (g_want_version) { write_str(STDOUT_FILENO, "crom 0.2.0\n"); return 0; }

    if (!target) target = ".";
    if (!pattern && !needle) pattern = "*";
    if (num_threads <= 0) num_threads = nproc();
    if (num_threads > SCAN_MAX_WORKERS) num_threads = SCAN_MAX_WORKERS;
    if (g_size_cmp == 0 && g_size_val > 0) g_size_cmp = 1;

    if (g_bar) g_bar = display_init(color_on(STDERR_FILENO));
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
    cfg.null_sep    = g_null_sep;
    cfg.use_ignore  = g_use_ignore;
    cfg.bar         = g_bar;
    cfg.tty_out     = is_tty(STDOUT_FILENO);
    cfg.num_workers = num_threads;

    i64 rfd = syscall3(SYS_openat, AT_FDCWD, (long)target,
                       O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (rfd < 0) {
        write_str(STDERR_FILENO, prog);
        write_str(STDERR_FILENO, ": cannot open '");
        write_str(STDERR_FILENO, target);
        write_str(STDERR_FILENO, "'\n");
        return 2;
    }
    syscall1(SYS_close, rfd);

    g_matches = scan_run(&scanner, &cfg, target);

    if (g_bar)
        display_done(scanner.n_dirs, scanner.n_files, g_matches, 0);

    if (scanner.err) {
        write_str(STDERR_FILENO, prog);
        write_str(STDERR_FILENO, ": scan failed\n");
        return 2;
    }
    return g_matches > 0 ? 0 : 1;   /* grep convention: 1 == no matches */
}
