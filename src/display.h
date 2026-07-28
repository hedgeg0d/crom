#ifndef CROM_DISPLAY_H
#define CROM_DISPLAY_H

#include "types.h"

/* Returns non-zero if stderr is a terminal, i.e. whether a bar makes sense. */
i64  display_init(i64 use_color);
void display_clear(void);
void display_update(i64 dirs, i64 files, i64 matches);
void display_done(i64 dirs, i64 files, i64 matches, i64 elapsed_us);

#endif
