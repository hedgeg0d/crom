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
    if (*p == '/') p++;
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
    if (plen == 0) return 0;

    r->anchored = (pat[0] == '/');
    r->pat = pat;
    return 1;
}

const GitNode *gitignore_load(Bump *ar, const GitNode *parent,
                              const char *dir, i64 dir_len) {
    char gpath[4096];
    if (dir_len + 12 >= (i64)sizeof(gpath)) return parent;

    i64 pos = 0;
    for (i64 i = 0; i < dir_len; i++) gpath[pos++] = dir[i];
    if (pos > 0 && gpath[pos-1] != '/') gpath[pos++] = '/';
    const char *g = ".gitignore";
    for (i64 i = 0; i < 10; i++) gpath[pos++] = g[i];
    gpath[pos] = 0;

    i32 fd = (i32)syscall3(SYS_openat, AT_FDCWD, (long)gpath, O_RDONLY|O_CLOEXEC);
    if (fd < 0) return parent;

    char rbuf[4096];
    i64 n = (i64)syscall3(SYS_read, fd, (long)rbuf, sizeof(rbuf) - 1);
    syscall1(SYS_close, fd);
    if (n <= 0) return parent;
    rbuf[n] = 0;

    GitRule tmp[GR_MAX];
    i64 cnt = 0;
    char *line = rbuf;
    char *lim = rbuf + n;
    while (line < lim && cnt < GR_MAX) {
        char *end = line;
        while (end < lim && *end != '\n') end++;
        if (end < lim) *end = 0;
        if (git_parse_line(line, &tmp[cnt])) cnt++;
        line = end + 1;
    }
    if (!cnt) return parent;

    i64 patbytes = 0;
    for (i64 i = 0; i < cnt; i++) patbytes += str_len(tmp[i].pat) + 1;

    GitNode *nd = (GitNode *)bump_alloc(ar,
        (i64)sizeof(GitNode) + cnt * (i64)sizeof(GitRule) + patbytes);
    if (!nd) return parent;   /* arena exhausted: fall back to inherited rules */

    char *pb = (char *)(nd->rules + cnt);
    for (i64 i = 0; i < cnt; i++) {
        i64 l = str_len(tmp[i].pat);
        for (i64 k = 0; k <= l; k++) pb[k] = tmp[i].pat[k];
        nd->rules[i] = tmp[i];
        nd->rules[i].pat = pb;
        pb += l + 1;
    }
    nd->parent = parent;
    nd->count = cnt;
    return nd;
}

i64 gitignore_check(const GitNode *n, const char *name, i32 is_dir) {
    for (; n; n = n->parent)
        for (i64 i = n->count - 1; i >= 0; i--)
            if (git_match_rule(&n->rules[i], name, is_dir))
                return n->rules[i].negate ? 0 : 1;
    return 0;
}
