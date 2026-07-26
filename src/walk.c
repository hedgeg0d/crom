#include "walk.h"
#include "syscalls.h"
#include "util.h"
#include "arena.h"

#define BUF_SZ 8192
#define PATH_SZ 4096

static i64 open_dir(const char *path) {
    return syscall3(SYS_openat, AT_FDCWD, (long)path,
                    O_RDONLY|O_DIRECTORY|O_CLOEXEC);
}

typedef struct {
    char buf[PATH_SZ];
    i64 len;
    walk_file_cb cb;
    void *ctx;
} WalkState;

static void walk_dir(WalkState *ws) {
    i64 fd = open_dir(ws->buf);
    if (fd < 0) return;

    char dbuf[BUF_SZ];
    if (ws->len == 0) {
        ws->buf[0] = '.';
        ws->buf[1] = '/';
        ws->buf[2] = 0;
        ws->len = 2;
    } else if (ws->buf[ws->len - 1] != '/') {
        ws->buf[ws->len] = '/';
        ws->buf[++ws->len] = 0;
    }
    i64 prefix_len = ws->len;

    for (;;) {
        i64 n = syscall3(SYS_getdents64, fd, (long)dbuf, BUF_SZ);
        if (n <= 0) break;

        for (i64 pos = 0; pos < n; ) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(dbuf + pos);
            const char *name = d->d_name;
            if (name[0] == '.') {
                if (name[1] == 0) goto skip;
                if (name[1] == '.' && name[2] == 0) goto skip;
            }

            i64 name_len = str_len(name);
            if (prefix_len + name_len >= PATH_SZ - 1) goto skip;

            for (i64 i = 0; i < name_len; i++)
                ws->buf[prefix_len + i] = name[i];
            ws->len = prefix_len + name_len;
            ws->buf[ws->len] = 0;

            ws->cb(ws->buf, ws->len, d->d_type, ws->ctx);

            if (d->d_type == DT_DIR) {
                walk_dir(ws);
            }

skip:
            pos += d->d_reclen;
        }
    }

    ws->buf[prefix_len] = 0;
    ws->len = prefix_len;

    syscall1(SYS_close, fd);
}

void walk(const char *root, walk_file_cb cb, void *ctx) {
    WalkState ws;
    ws.len = 0;
    ws.cb = cb;
    ws.ctx = ctx;

    if (root && root[0]) {
        i64 rl = str_len(root);
        if (rl >= PATH_SZ - 1) return;
        for (i64 i = 0; i < rl; i++) ws.buf[i] = root[i];
        ws.len = rl;
        ws.buf[rl] = 0;
    }

    walk_dir(&ws);
}
