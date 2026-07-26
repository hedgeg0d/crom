#ifndef CROM_DISPLAY_H
#define CROM_DISPLAY_H

#include "types.h"

void display_init(void);
void display_update(i64 dirs, i64 files, i64 matches);
void display_done(i64 dirs, i64 files, i64 matches, i64 elapsed_us);

#endif
