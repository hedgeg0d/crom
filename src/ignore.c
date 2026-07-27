#include "ignore.h"
#include "syscalls.h"
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
    "build",
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

static i64 git_match_rule(const GitRule *r, const char *name, i32 is_dir) {
    const char *p = r->pat;
    if (*p == '/') p++; // anchored, already consumed by caller logic
    if (!*p) return 1;

    if (r->dir_only && !is_dir) return 0;

    if (*p == '*') {
        p++;
        if (!*p) return 1;
        i64 nl = str_len(name);
        i64 pl = str_len(p);
        if (nl >= pl) {
            for (i64 i = 0; i < pl; i++)
                if (name[nl - pl + i] != p[i]) return 0;
            return 1;
        }
        return 0;
    }

    const char *n = name;
    while (*p && *n && *p == *n) { p++; n++; }
    return *p == 0 && (*n == 0 || *n == '/');
}

static i64 git_parse_line(char *line, GitRule *r) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == 0 || *line == '#' || *line == '\n') return 0;
    if (*line == '!') { r->negate = 1; line++; }
    else r->negate = 0;

    char *pat = line;
    i64 plen = str_len(pat);
    while (plen > 0 && (pat[plen-1] == '\n' || pat[plen-1] == ' ')) plen--;
    pat[plen] = 0;

    r->dir_only = 0;
    if (plen > 0 && pat[plen-1] == '/') { r->dir_only = 1; plen--; pat[plen] = 0; }

    r->anchored = (pat[0] == '/');
    r->pat = pat;
    return 1;
}

void gitignore_load(GitList *gl, const char *dir_path, i64 dir_len) {
    gl->count = 0;
    gl->buf_len = 0;

    char gpath[4096];
    i64 pos = 0;
    for (i64 i = 0; i < dir_len; i++) gpath[pos++] = dir_path[i];
    if (pos > 0 && gpath[pos-1] != '/') gpath[pos++] = '/';
    gpath[pos++] = '.'; gpath[pos++] = 'g'; gpath[pos++] = 'i';
    gpath[pos++] = 't'; gpath[pos++] = 'i'; gpath[pos++] = 'g';
    gpath[pos++] = 'n'; gpath[pos++] = 'o'; gpath[pos++] = 'r';
    gpath[pos++] = 'e'; gpath[pos] = 0;

    i32 fd = (i32)syscall3(SYS_openat, AT_FDCWD, (long)gpath, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return;

    char rbuf[4096];
    i64 n = (i64)syscall3(SYS_read, fd, (long)rbuf, sizeof(rbuf) - 1);
    syscall1(SYS_close, fd);
    if (n <= 0) return;
    rbuf[n] = 0;

    char *line = rbuf;
    while (line < rbuf + n && gl->count < GR_MAX) {
        char *end = line;
        while (end < rbuf + n && *end != '\n') end++;
        if (end < rbuf + n) *end = 0;

        GitRule r;
        if (git_parse_line(line, &r)) {
            i64 pl = str_len(r.pat);
            for (i64 i = 0; i < pl; i++) gl->buf[gl->buf_len++] = r.pat[i];
            gl->buf[gl->buf_len++] = 0;
            r.pat = gl->buf + gl->buf_len - pl - 1;
            gl->rules[gl->count++] = r;
        }

        line = end + 1;
    }
}

i64 gitignore_check(GitList *gl, const char *name, i32 is_dir) {
    i64 result = 0;
    for (i64 i = 0; i < gl->count; i++) {
        GitRule *r = &gl->rules[i];
        if (git_match_rule(r, name, is_dir))
            result = r->negate ? 0 : 1;
    }
    return result;
}
