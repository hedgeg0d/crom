#ifndef CROM_MATCH_CONTENT_H
#define CROM_MATCH_CONTENT_H

#include "types.h"

i64 content_search(const u8 *data, i64 len, const char *needle, i64 nlen);
i64 search_file(const char *path, const char *needle, i64 nlen, u8 *rbuf, i64 rbuf_sz);

#endif
