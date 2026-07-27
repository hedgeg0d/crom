#ifndef CROM_POOL_H
#define CROM_POOL_H

#include "types.h"
#include "arena.h"

#define POOL_QUEUE_CAP 1024
#define POOL_PATH_SZ   1024

typedef struct {
    char path[POOL_PATH_SZ];
    i64 len;
    u8 dtype;
} PoolItem;

typedef struct {
    PoolItem items[POOL_QUEUE_CAP];
    volatile i64 head;
    volatile i64 tail;
    volatile i64 done;
    volatile i64 pending;
    volatile i64 matches;
    i64 num_workers;
} Pool;

void pool_init(Pool *p, i64 workers);
void pool_spawn(Pool *p, const char *needle, i64 nlen);
i64  pool_push(Pool *p, const char *path, i64 len, u8 dtype);
i64  pool_pop(Pool *p, char **path, i64 *len, u8 *dtype);
i64  pool_try_pop(Pool *p, char **path, i64 *len, u8 *dtype);
void pool_flush(Pool *p);

#endif
