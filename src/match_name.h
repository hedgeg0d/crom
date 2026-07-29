#ifndef CROM_MATCH_NAME_H
#define CROM_MATCH_NAME_H

#include "types.h"

#define CASE_SMART     0   /* insensitive until the pattern has a capital */
#define CASE_SENSITIVE 1
#define CASE_IGNORE    2

/* Pattern worked out up front: one of these per pattern and per --exclude. */
typedef struct {
    const char *pat;
    i64 plen;
    i64 substr;         /* match anywhere in the name instead of all of it */
    i64 meta;           /* pattern holds * ? or [ */
    i64 pfxl;           /* literal run before the first metacharacter */
    const char *sfx;    /* literal tail after the last '*', or 0 */
    i64 sfxl;
    u8 fold[256];       /* identity map when the match is case-sensitive */
} NameMatcher;

/* force_glob (-g) keeps a metacharacter-free pattern anchored to the whole name. */
void name_compile(NameMatcher *m, const char *pattern, i64 case_mode,
                  i64 force_glob);

/* nlen is the name's length, which every caller already knows. */
i64 name_match(const NameMatcher *m, const char *name, i64 nlen);

#endif
