#ifndef CROM_IOURING_H
#define CROM_IOURING_H

#include "types.h"
#include "syscalls.h"

#define IORING_OFF_SQ_RING  0
#define IORING_OFF_CQ_RING  0x8000000
#define IORING_OFF_SQES     0x10000000

#define IORING_SETUP_SQPOLL 2

#define IORING_OP_OPENAT    18
#define IORING_OP_CLOSE     19
#define IORING_OP_STATX     21
#define IORING_OP_READ      22
#define IORING_OP_OPENAT2   28

#define IOSQE_FIXED_FILE    (1U << 0)
#define IOSQE_IO_LINK       (1U << 2)
#define IOSQE_IO_HARDLINK   (1U << 3)

#define IORING_REGISTER_FILES 2

struct iouring_sqe {
    u8  opcode;
    u8  flags;
    u16 ioprio;
    i32 fd;
    u64 off;
    u64 addr;
    u32 len;
    u32 open_flags;
    u64 user_data;
    u16 buf_index;
    u16 personality;
    u32 file_index;
    u64 addr3;
    u64 __pad2;
};

struct iouring_cqe {
    u64 user_data;
    i32 res;
    u32 flags;
};

struct iouring_sq_off {
    u32 head;
    u32 tail;
    u32 ring_mask;
    u32 ring_entries;
    u32 flags;
    u32 dropped;
    u32 array;
    u32 resv1;
    u64 user_addr;
};

struct iouring_cq_off {
    u32 head;
    u32 tail;
    u32 ring_mask;
    u32 ring_entries;
    u32 overflow;
    u32 cqes;
    u32 flags;
    u32 resv1;
    u64 user_addr;
};

struct iouring_params {
    u32 sq_entries;
    u32 cq_entries;
    u32 flags;
    u32 sq_thread_cpu;
    u32 sq_thread_idle;
    u32 features;
    u32 wq_fd;
    u32 resv[3];
    struct iouring_sq_off sq_off;
    struct iouring_cq_off cq_off;
};

typedef struct {
    i32 fd;
    u32 ring_entries;
    u32 sq_mask;
    u32 cq_mask;
    u32 *sq_head;
    u32 *sq_tail;
    u32 *sq_array;
    u32 *cq_head;
    u32 *cq_tail;
    struct iouring_cqe *cqes;
    struct iouring_sqe *sqes;
    u32 sqe_head;
    u32 sqe_tail;
} IOUring;

i32 iouring_init(IOUring *r, u32 entries);
i32 iouring_register_sparse_files(IOUring *r, u32 nr);
void iouring_free(IOUring *r);
u32 iouring_sq_space(IOUring *r);
struct iouring_sqe *iouring_get_sqe(IOUring *r);
i32 iouring_submit_and_wait(IOUring *r, u32 wait_nr);
i32 iouring_wait(IOUring *r, u32 nr);
i32 iouring_peek_cqe(IOUring *r, struct iouring_cqe **cqe);
void iouring_cqe_seen(IOUring *r, struct iouring_cqe *cqe);

#endif
