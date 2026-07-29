#ifndef CROM_POOL_H
#define CROM_POOL_H

#include "types.h"
#include "arena.h"
#include "ignore.h"

/* Anything that made the results incomplete. A scan that trips one of these is
   still printed, but crom says so on stderr and exits 2: silently returning a
   short answer is worse than saying the answer is short. */
#define ERR_FATAL     1   /* nothing was scanned at all */
#define ERR_OPENDIR   2   /* a directory could not be opened */
#define ERR_ARENA     4   /* path arena exhausted, subtrees dropped */
#define ERR_TOOLONG   8   /* path would exceed PBUF_SZ, subtree dropped */
#define ERR_EXEC     16   /* -e command did not fit its buffer, not run */

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
    i64 quiet_errs;           /* --no-messages: count problems, don't print */
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
    volatile i64 err;         /* ERR_* bits, OR-ed from every worker */
} Scanner;

/* Runs the whole traversal; the calling thread participates as a worker.
   Returns the number of matches.

   The Scanner must be zero-filled going in (a static or freshly mapped one
   is): the work queue reads its empty state straight out of that zero image
   instead of writing every slot before the search starts, so scan_run may be
   called only once per Scanner.

   All roots are seeded before the first worker runs, so depth limits are
   counted from each root separately. */
i64 scan_run(Scanner *s, const ScanCfg *cfg, const char **roots, i64 nroots);

#endif
