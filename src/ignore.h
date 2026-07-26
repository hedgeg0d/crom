#ifndef CROM_IGNORE_H
#define CROM_IGNORE_H

#include "types.h"

void ignore_set_enabled(i64 on);
i64 match_ignore(const char *name, i32 is_dir);

#endif
