#include "syscalls.h"
#include "types.h"
#include "util.h"
#include "walk.h"
#include "match_name.h"
#include "pool.h"
#include "display.h"
#include "ignore.h"

static const char *prog = "crom";
static const char *pattern;
static const char *needle;
static i64 needle_len;
static i64 num_threads;
static Pool pool_data;
static Pool *pool;
static volatile i64 g_dirs;
static volatile i64 g_files;
static volatile i64 g_matches;
static i64 g_bar;
static i64 g_use_ignore = 1;

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

static void on_file(const char *path, i64 len, u8 dtype, void *ctx) {
    (void)ctx;

    if (dtype == DT_DIR) {
        g_dirs++;
        if (g_bar && (g_dirs & 63) == 0) display_update(g_dirs, g_files, g_matches);
    } else if (dtype == DT_REG) {
        g_files++;
    } else {
        return;
    }

    if (dtype != DT_REG) return;

    if (pattern) {
        const char *name = path + len;
        while (name > path && name[-1] != '/') name--;
        if (!match_glob(pattern, name)) return;
    }

    if (pool) {
        pool_push(pool, path, len, dtype);
    } else {
        g_matches++;
        if (g_bar && (g_matches & 15) == 0) display_update(g_dirs, g_files, g_matches);
        write_all(STDOUT_FILENO, path, len);
        write_all(STDOUT_FILENO, "\n", 1);
    }
}

static i64 parse_int(const char *s) {
    i64 v = 0;
    for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (*s - '0');
    return v;
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
        if (str_eq(argv[i], "--bar")) {
            g_bar = 1;
            continue;
        }
        if (str_eq(argv[i], "--no-ignore")) {
            g_use_ignore = 0;
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
    if (num_threads <= 0) num_threads = 1;

    if (g_bar) display_init();
    ignore_set_enabled(g_use_ignore);

    if (needle) {
        pool = &pool_data;
        pool_init(pool, num_threads);
        pool_spawn(pool, needle, needle_len);
    }

    walk(target, on_file, 0);

    if (pool) {
        pool_flush(pool);
        g_matches = pool->matches;
    }

    if (g_bar) display_done(g_dirs, g_files, g_matches, 0);

    return 0;
}
