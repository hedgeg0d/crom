#ifndef CROM_POOL_H
#define CROM_POOL_H

#include "types.h"
#include "arena.h"
#include "ignore.h"

/* Backlog the workers share. 1024 pending directories is far more than eight
   threads ever hold at once, and anything bigger only made the ring's pages
   more expensive to fault in; overflow spills to the finder's own stack. */
#define SCAN_QUEUE_CAP 1024
#define SCAN_MAX_WORKERS 256

typedef struct {
    volatile i64 dirs, files, matches;
    i64 _pad[5];
} __attribute__((aligned(64))) WCount;

typedef struct {
    const char *path;      /* arena-owned, NUL-terminated */
    i32 len;
    i32 depth;
    const GitNode *ign;
} DirRef;

typedef struct {
    DirRef d;
    volatile i64 seq;
} ScanSlot;

typedef struct {
    const char *pattern;      /* name glob, or 0 */
    const char *needle;       /* content substring, or 0 */
    i64 needle_len;
    const char *exec_cmd;     /* -e, or 0 */
    i64 type_filter;          /* 0 | 'f' | 'd' | 'l' */
    i64 size_cmp;             /* -1 lt, 0 off, 1 gt */
    i64 size_val;
    i64 max_depth;            /* -1 = unlimited */
    i64 json_out;
    i64 null_sep;  /* -0: terminate records with NUL instead of newline */
    i64 use_ignore;
    i64 bar;
    i64 tty_out;   /* stdout is a terminal -> flush every line */
    i64 num_workers;
} ScanCfg;

typedef struct {
    const ScanCfg *cfg;
    Bump arena;
    WCount wc[SCAN_MAX_WORKERS];

    ScanSlot q[SCAN_QUEUE_CAP];
    volatile i64 head;
    volatile i64 tail;

    volatile i64 active;      /* dirs discovered but not yet finished */
    volatile i64 done;
    volatile i64 work_gen;    /* bumped on every publish, futex address */
    volatile i64 idle;

    volatile i64 n_dirs;
    volatile i64 n_files;
    volatile i64 n_matches;

    volatile i64 workers_live;
    volatile i64 workers_exited;
    volatile i64 bar_live;
    volatile i64 bar_exited;
    volatile i64 next_id;
    volatile i64 spawned;     /* worker threads created so far (lazily) */
    volatile i64 err;         /* fatal: no arena, root unreadable */
} Scanner;

/* Runs the whole traversal; the calling thread participates as a worker.
   Returns the number of matches.

   The Scanner must be zero-filled going in (a static or freshly mapped one
   is): the work queue reads its empty state straight out of that zero image
   instead of writing every slot before the search starts, so scan_run may be
   called only once per Scanner. */
i64 scan_run(Scanner *s, const ScanCfg *cfg, const char *root);

#endif
