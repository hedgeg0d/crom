#include "syscalls.h"
#include "types.h"
#include "util.h"
#include "arena.h"
#include "walk.h"

static const char *prog = "crom";

static void usage(void) {
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

static void print_file(const char *path, i64 len, u8 dtype, void *ctx) {
    (void)ctx;
    if (dtype != DT_REG) return;
    write_all(STDOUT_FILENO, path, len);
    write_all(STDOUT_FILENO, "\n", 1);
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

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-h") || str_eq(argv[i], "--help")) {
            usage();
            return 0;
        }
        if (str_eq(argv[i], "-V") || str_eq(argv[i], "--version")) {
            write_str(STDOUT_FILENO, "crom 0.1.0\n");
            return 0;
        }
    }

    const char *target = ".";
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            target = argv[i];
            break;
        }
    }

    walk(target, print_file, 0);
    return 0;
}
