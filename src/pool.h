#ifndef CROM_POOL_H
#define CROM_POOL_H

#include "types.h"
#include "arena.h"

#define POOL_QUEUE_CAP 1024
#define POOL_PATH_SZ   1024

/* Slot generation counter, Vyukov-style bounded queue.
   seq == h        -> slot is free, producer may fill generation h
   seq == h + 1    -> slot holds published data for consumer h
   seq == h + CAP  -> consumer copied the data out, slot free again
   Without this handshake the producer reuses a slot as soon as `tail`
   moves, while the consumer is still reading the path out of it. */
typedef struct {
    char path[POOL_PATH_SZ];
    i64 len;
    u8 dtype;
    volatile i64 seq;
} PoolItem;

typedef struct {
    PoolItem items[POOL_QUEUE_CAP];
    volatile i64 head;
    volatile i64 tail;
    volatile i64 done;
    volatile i64 pending;
    volatile i64 waiters;
    volatile i64 matches;
    volatile i64 workers_live;
    volatile i64 workers_exited;
    i64 num_workers;
    i64 json_out;
} Pool;

void pool_init(Pool *p, i64 workers);
void pool_spawn(Pool *p, const char *needle, i64 nlen);
i64  pool_push(Pool *p, const char *path, i64 len, u8 dtype);
/* `path` is a caller-owned buffer of at least POOL_PATH_SZ bytes; the payload
   is copied into it so the caller may hold it past the slot's lifetime. */
i64  pool_pop(Pool *p, char *path, i64 *len, u8 *dtype);
i64  pool_try_pop(Pool *p, char *path, i64 *len, u8 *dtype);
void pool_flush(Pool *p);

#endif
