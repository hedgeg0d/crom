#ifndef CROM_TYPES_H
#define CROM_TYPES_H

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef int i32;
typedef unsigned char u8;

typedef struct {
    void *buf;
    i64 cap;
    i64 len;
    i64 pos;
} Arena;

#endif
