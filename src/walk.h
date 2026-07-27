#ifndef CROM_WALK_H
#define CROM_WALK_H

#include "types.h"

struct linux_dirent64 {
    u64  d_ino;
    i64  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char  d_name[];
};

typedef void (*walk_file_cb)(const char *path, i64 len, u8 d_type, void *ctx);

void walk(const char *root, walk_file_cb cb, void *ctx, i64 max_depth);

#endif
