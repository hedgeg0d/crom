#include "iouring.h"

i32 iouring_init(IOUring *r, u32 entries) {
    struct iouring_params p;
    for (i64 i = 0; i < (i64)sizeof(p); i++) ((u8 *)&p)[i] = 0;

    i32 fd = (i32)syscall2(SYS_io_uring_setup, entries, (long)&p);
    if (fd < 0) return -1;

    r->fd = fd;
    r->ring_entries = p.sq_entries;

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

    r->sq_mask = *(u32 *)((u8 *)sq + p.sq_off.ring_mask);
    r->cq_mask = *(u32 *)((u8 *)cq + p.cq_off.ring_mask);
    r->cqes = (struct iouring_cqe *)((u8 *)cq + p.cq_off.cqes);
    r->sqes = (struct iouring_sqe *)sqes;
    r->sqe_head = 0;
    r->sqe_tail = 0;

    return 0;
}

i32 iouring_register_sparse_files(IOUring *r, u32 nr) {
    i32 fds[256];
    if (nr > 256) return -1;
    for (u32 i = 0; i < nr; i++) fds[i] = -1;   /* -1 == sparse slot */

    long rc = syscall4(SYS_io_uring_register, r->fd,
                       IORING_REGISTER_FILES, (long)fds, nr);
    return rc < 0 ? (i32)rc : 0;
}

void iouring_free(IOUring *r) {
    if (r->fd >= 0) syscall1(SYS_close, r->fd);
}

u32 iouring_sq_space(IOUring *r) {
    return r->ring_entries - (r->sqe_head - r->sqe_tail);
}

struct iouring_sqe *iouring_get_sqe(IOUring *r) {
    if (r->sqe_head - r->sqe_tail >= r->ring_entries) return 0;

    struct iouring_sqe *sqe = &r->sqes[r->sqe_head & r->sq_mask];
    r->sqe_head++;

    for (i64 i = 0; i < (i64)sizeof(*sqe); i++) ((u8 *)sqe)[i] = 0;

    return sqe;
}

i32 iouring_submit_and_wait(IOUring *r, u32 wait_nr) {
    u32 to_submit = r->sqe_head - r->sqe_tail;

    if (to_submit == 0) {
        if (wait_nr == 0) return 0;
        return iouring_wait(r, wait_nr);
    }

    for (u32 i = r->sqe_tail; i != r->sqe_head; i++)
        r->sq_array[i & r->sq_mask] = i & r->sq_mask;

    __asm__ volatile ("" ::: "memory");
    *r->sq_tail = r->sqe_head;
    __asm__ volatile ("" ::: "memory");

    u32 total = 0;
    while (total < to_submit) {
        u32 want = wait_nr;
        u32 flags = want ? IORING_ENTER_GETEVENTS : 0;

        i32 n = (i32)syscall6(SYS_io_uring_enter, r->fd,
                              to_submit - total, want, flags, 0, 0);
        if (n < 0) {
            if (n == -4 || n == -11) continue;
            r->sqe_tail += total;
            *r->sq_tail = r->sqe_tail;
            __asm__ volatile ("" ::: "memory");
            r->sqe_head = r->sqe_tail;
            return total ? (i32)total : n;
        }
        if (n == 0) break;
        total += (u32)n;
    }

    r->sqe_tail += total;
    return (i32)total;
}

i32 iouring_wait(IOUring *r, u32 nr) {
    for (;;) {
        __asm__ volatile ("" ::: "memory");
        u32 done = *r->cq_tail - *r->cq_head;
        if (done >= nr) return 0;

        i32 n = (i32)syscall6(SYS_io_uring_enter, r->fd, 0, nr - done,
                              IORING_ENTER_GETEVENTS, 0, 0);
        if (n < 0 && n != -4 /*EINTR*/ && n != -11 /*EAGAIN*/) return n;
    }
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
