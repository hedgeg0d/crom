#ifndef CROM_WALK_H
#define CROM_WALK_H

#include "types.h"

struct linux_dirent64 {
    u64  d_ino;
    i64  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char  d_name[];
};

#endif
