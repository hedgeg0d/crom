#ifndef CROM_SYSCALLS_H
#define CROM_SYSCALLS_H

#define SYS_read           0
#define SYS_write          1
#define SYS_open           2
#define SYS_close          3
#define SYS_fstat          5
#define SYS_lseek          8
#define SYS_mmap           9
#define SYS_mremap        25
#define SYS_munmap        11
#define SYS_pread64       17
#define SYS_brk           12
#define SYS_nanosleep     35
#define SYS_getpid        39
#define SYS_clone         56
#define SYS_fork          57
#define SYS_vfork         58
#define SYS_execve        59
#define SYS_exit          60
#define SYS_wait4         61
#define SYS_fcntl         72
#define SYS_fsync         74
#define SYS_ftruncate     77
#define SYS_getdents64   217
#define SYS_gettid       186
#define SYS_futex        202
#define SYS_sched_yield   24
#define SYS_exit_group   231
#define SYS_openat       257
#define SYS_mkdirat      258
#define SYS_unlinkat     263
#define SYS_readlinkat   267
#define SYS_statx        332
#define SYS_io_uring_setup   425
#define SYS_io_uring_enter   426
#define SYS_io_uring_register 427
#define SYS_openat2          437
#define SYS_sched_getaffinity 204
#define SYS_getrandom        318
#define SYS_clock_gettime    228

#define AT_FDCWD  ((long)-100)
#define AT_EMPTY_PATH  0x1000
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_NO_AUTOMOUNT 0x800

#define O_RDONLY   00000000
#define O_WRONLY   00000001
#define O_RDWR     00000002
#define O_CREAT    00000100
#define O_EXCL     00000200
#define O_NOCTTY   00000400
#define O_TRUNC    00001000
#define O_APPEND   00002000
#define O_NONBLOCK 00004000
#define O_DSYNC    00010000
#define O_DIRECT   00040000
#define O_LARGEFILE 00100000
#define O_DIRECTORY 00200000
#define O_NOFOLLOW  00400000
#define O_CLOEXEC   02000000

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

#define STATX_TYPE      0x0001
#define STATX_MODE      0x0002
#define STATX_SIZE      0x0008
#define STATX_MTIME     0x0400
#define STATX_CTIME     0x0080
#define STATX_ATIME     0x0020
#define STATX_BTIME     0x0800
#define STATX_BASIC_STATS (STATX_TYPE|STATX_MODE|STATX_SIZE|STATX_MTIME)

#define S_IFMT   00170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_POPULATE  0x08000
#define MAP_FAILED    ((void *)-1)

static inline long is_mmap_err(void *p) {
    long v = (long)p;
    return v < 0 && v > -4096;
}

#define FUTEX_WAIT        0
#define FUTEX_WAKE        1
#define FUTEX_PRIVATE_FLAG 128

#define CLONE_VM          0x00000100
#define CLONE_FS          0x00000200
#define CLONE_FILES       0x00000400
#define CLONE_SIGHAND     0x00000800
#define CLONE_THREAD      0x00010000
#define CLONE_SETTLS      0x00080000
#define CLONE_PARENT_SETTID 0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETPTID  0x00001000

#define IORING_SETUP_SQPOLL    2
#define IORING_SETUP_SQ_AFF    4
#define IORING_ENTER_GETEVENTS 1
#define IORING_ENTER_SQ_WAKEUP 2
#define IORING_OP_NOP       0
#define IORING_OP_READV     1
#define IORING_OP_WRITEV    2
#define IORING_OP_FSYNC     3
#define IORING_OP_READ_FIXED     4
#define IORING_OP_WRITE_FIXED    5
#define IORING_OP_OPENAT    18
#define IORING_OP_CLOSE     19
#define IORING_OP_STATX     30
#define IOSQE_FIXED_FILE    (1U << 0)

#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

#define CLOCK_MONOTONIC 1
#define CLOCK_REALTIME  0

struct stat64 {
    unsigned long st_dev;
    unsigned long st_ino;
    unsigned long st_nlink;
    unsigned int  st_mode;
    unsigned int  st_uid;
    unsigned int  st_gid;
    unsigned int  __pad0;
    unsigned long st_rdev;
    long          st_size;
    long          st_blksize;
    long          st_blocks;
};

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

static inline long syscall0(long n) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 asm("r10") = a4;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return ret;
}

#endif
