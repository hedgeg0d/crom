#ifndef CROM_MATCH_CONTENT_H
#define CROM_MATCH_CONTENT_H

#include "types.h"

void content_prepare(const char *needle, i64 nlen);
void content_set_text_only(i64 on);
i64 content_search(const u8 *data, i64 len, const char *needle, i64 nlen);
i64 search_file_at(i64 dirfd, const char *name, const char *needle, i64 nlen,
                   u8 *rbuf, i64 rbuf_sz);

#endif
