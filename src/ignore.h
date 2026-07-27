#ifndef CROM_IGNORE_H
#define CROM_IGNORE_H

#include "types.h"

void ignore_set_enabled(i64 on);
i64 match_ignore(const char *name, i32 is_dir);

#define GR_MAX 64
typedef struct {
    char *pat;
    i32 negate;
    i32 dir_only;
    i32 anchored;
} GitRule;

typedef struct {
    GitRule rules[GR_MAX];
    i64 count;
    char buf[4096];
    i64 buf_len;
} GitList;

void gitignore_load(GitList *gl, const char *dir_path, i64 dir_len);
i64 gitignore_check(GitList *gl, const char *name, i32 is_dir);

#endif
