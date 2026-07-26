#ifndef CROM_ATOMICS_H
#define CROM_ATOMICS_H

#include "types.h"

static inline i64 atomic_load(volatile i64 *p) {
    i64 v;
    __asm__ volatile ("mov %1, %0" : "=r"(v) : "m"(*p) : "memory");
    return v;
}

static inline void atomic_store(volatile i64 *p, i64 v) {
    __asm__ volatile ("mov %1, %0" : "=m"(*p) : "r"(v) : "memory");
}

static inline i64 atomic_xadd(volatile i64 *p, i64 v) {
    __asm__ volatile ("lock xadd %0, %1" : "+r"(v), "+m"(*p) : : "memory");
    return v;
}

static inline i64 atomic_cas(volatile i64 *p, i64 exp, i64 des) {
    i64 old = exp;
    __asm__ volatile ("lock cmpxchg %2, %1"
                      : "+a"(old), "+m"(*p) : "r"(des) : "memory");
    return old;
}

#endif
