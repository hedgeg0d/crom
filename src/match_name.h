#ifndef CROM_MATCH_NAME_H
#define CROM_MATCH_NAME_H

#include "types.h"

#define CASE_SMART     0   /* insensitive until the pattern has a capital */
#define CASE_SENSITIVE 1
#define CASE_IGNORE    2

/* Decides once how the pattern is to be read, so the per-file matcher has no
   choices left to make. force_glob comes from -g: it keeps a metacharacter-free
   pattern anchored to the whole name instead of matching a substring. */
void name_prepare(const char *pattern, i64 case_mode, i64 force_glob);

/* nlen is the name's length, which every caller already knows. */
i64 match_name(const char *name, i64 nlen);

#endif
