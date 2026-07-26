#include "ignore.h"
#include "util.h"

static i64 g_enabled = 1;

static const char *DIRS[] = {
    ".git",
    "node_modules",
    "__pycache__",
    ".svn",
    ".hg",
    ".idea",
    ".vscode",
    ".mypy_cache",
    0
};

void ignore_set_enabled(i64 on) { g_enabled = on; }

i64 match_ignore(const char *name, i32 is_dir) {
    if (!g_enabled) return 0;
    if (!is_dir) return 0;
    for (i64 k = 0; DIRS[k]; k++) {
        if (str_eq(name, DIRS[k])) return 1;
    }
    return 0;
}
