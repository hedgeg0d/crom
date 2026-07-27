#include "iouring.h"

i32 iouring_init(IOUring *r, u32 entries) {
    struct iouring_params p;
    for (i64 i = 0; i < (i64)sizeof(p); i++) ((u8 *)&p)[i] = 0;

    i32 fd = (i32)syscall2(SYS_io_uring_setup, entries, (long)&p);
    if (fd < 0) return -1;

    r->fd = fd;
    r->ring_entries = p.sq_entries;
    r->sq_mask = p.sq_off.ring_mask;
    r->cq_mask = p.cq_off.ring_mask;

    i64 sq_sz = p.sq_off.array + p.sq_entries * sizeof(u32);
    void *sq = (void *)syscall6(SYS_mmap, 0, (unsigned long)sq_sz,
                                 PROT_READ|PROT_WRITE,
                                 MAP_SHARED|MAP_POPULATE, fd,
                                 IORING_OFF_SQ_RING);
    if (is_mmap_err(sq)) { syscall1(SYS_close, fd); return -1; }

    i64 cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct iouring_cqe);
    void *cq = (void *)syscall6(SYS_mmap, 0, (unsigned long)cq_sz,
                                 PROT_READ|PROT_WRITE,
                                 MAP_SHARED|MAP_POPULATE, fd,
                                 IORING_OFF_CQ_RING);
    if (is_mmap_err(cq)) {
        syscall2(SYS_munmap, (long)sq, (unsigned long)sq_sz);
        syscall1(SYS_close, fd);
        return -1;
    }

    i64 sqe_sz = p.sq_entries * sizeof(struct iouring_sqe);
    void *sqes = (void *)syscall6(SYS_mmap, 0, (unsigned long)sqe_sz,
                                   PROT_READ|PROT_WRITE,
                                   MAP_SHARED|MAP_POPULATE, fd,
                                   IORING_OFF_SQES);
    if (is_mmap_err(sqes)) {
        syscall2(SYS_munmap, (long)cq, (unsigned long)cq_sz);
        syscall2(SYS_munmap, (long)sq, (unsigned long)sq_sz);
        syscall1(SYS_close, fd);
        return -1;
    }

    r->sq_head = (u32 *)((u8 *)sq + p.sq_off.head);
    r->sq_tail = (u32 *)((u8 *)sq + p.sq_off.tail);
    r->sq_array = (u32 *)((u8 *)sq + p.sq_off.array);
    r->cq_head = (u32 *)((u8 *)cq + p.cq_off.head);
    r->cq_tail = (u32 *)((u8 *)cq + p.cq_off.tail);
    r->cqes = (struct iouring_cqe *)((u8 *)cq + p.cq_off.cqes);
    r->sqes = (struct iouring_sqe *)sqes;
    r->sqe_head = 0;
    r->sqe_tail = 0;

    return 0;
}

void iouring_free(IOUring *r) {
    if (r->fd >= 0) syscall1(SYS_close, r->fd);
}

void iouring_sync(IOUring *r) {
    r->sqe_head = *r->sq_head;
    r->sqe_tail = r->sqe_head;
}

struct iouring_sqe *iouring_get_sqe(IOUring *r) {
    u32 head = *r->sq_head;
    u32 next = r->sqe_head + 1;

    if (next - head > r->ring_entries) return 0;

    struct iouring_sqe *sqe = &r->sqes[r->sqe_head & r->sq_mask];
    r->sqe_head = next;

    for (i64 i = 0; i < (i64)sizeof(*sqe); i++) ((u8 *)sqe)[i] = 0;

    return sqe;
}

void iouring_submit(IOUring *r) {
    u32 submitted = r->sqe_tail;
    u32 to_submit = r->sqe_head - submitted;

    if (to_submit == 0) return;

    for (u32 i = submitted; i < r->sqe_head; i++)
        r->sq_array[i & r->sq_mask] = i & r->sq_mask;

    r->sqe_tail = r->sqe_head;

    __asm__ volatile ("" ::: "memory");
    *r->sq_tail = r->sqe_head;
    __asm__ volatile ("" ::: "memory");

    syscall3(SYS_io_uring_enter, r->fd, to_submit, to_submit);
}

i32 iouring_wait(IOUring *r, u32 nr) {
    __asm__ volatile ("" ::: "memory");
    u32 head = *r->cq_head;
    u32 tail = *r->cq_tail;
    u32 done = tail - head;

    if (done >= nr) return 0;

    i32 want = (i32)(nr - done);
    i32 ret = (i32)syscall3(SYS_io_uring_enter, r->fd, 0, want);
    if (ret < 0) return -1;

    return 0;
}

i32 iouring_peek_cqe(IOUring *r, struct iouring_cqe **cqe) {
    __asm__ volatile ("" ::: "memory");
    u32 head = *r->cq_head;
    u32 tail = *r->cq_tail;

    if (head == tail) return 0;

    *cqe = &r->cqes[head & r->cq_mask];
    return 1;
}

void iouring_cqe_seen(IOUring *r, struct iouring_cqe *cqe) {
    (void)cqe;
    u32 head = *r->cq_head;
    __asm__ volatile ("" ::: "memory");
    *r->cq_head = head + 1;
}
