#ifndef CROM_IGNORE_H
#define CROM_IGNORE_H

#include "types.h"
#include "arena.h"

void ignore_set_enabled(i64 on);
i64 match_ignore(const char *name, i32 is_dir);

#define GR_MAX 64

typedef struct {
    const char *pat;
    i32 negate;
    i32 dir_only;
    i32 anchored;
} GitRule;

/* One node per directory that actually has a .gitignore, linked to the node
   of the nearest such ancestor. Nodes are immutable and arena-owned, so any
   number of workers can walk different branches concurrently. */
typedef struct GitNode {
    const struct GitNode *parent;
    i64 count;
    GitRule rules[];
} GitNode;

/* Returns `parent` unchanged when dir has no usable .gitignore. */
const GitNode *gitignore_load(Bump *ar, const GitNode *parent,
                              const char *dir, i64 dir_len);
i64 gitignore_check(const GitNode *n, const char *name, i32 is_dir);

#endif
