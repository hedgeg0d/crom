#include "match_name.h"

static i64 has_meta(const char *p) {
    for (; *p; p++) {
        if (*p == '*' || *p == '?' || *p == '[') return 1;
    }
    return 0;
}

static i64 len_of(const char *s) {
    i64 n = 0;
    while (s[n]) n++;
    return n;
}

/* Folding is a table, identity when case-sensitive, so both modes share one
   code path. All of this used to be recomputed for every candidate name. */
void name_compile(NameMatcher *m, const char *pattern, i64 case_mode,
                  i64 force_glob) {
    for (i64 i = 0; i < 256; i++) m->fold[i] = (u8)i;
    m->pat = pattern;
    m->plen = 0; m->substr = 0; m->meta = 0;
    m->pfxl = 0; m->sfx = 0; m->sfxl = 0;
    if (!pattern) return;

    m->plen = len_of(pattern);
    m->meta = has_meta(pattern);

    /* A bare word means "name contains this"; * ? [ make it a whole-name glob. */
    m->substr = !force_glob && !m->meta;

    i64 icase;
    if (case_mode == CASE_SENSITIVE)   icase = 0;
    else if (case_mode == CASE_IGNORE) icase = 1;
    else {
        icase = 1;
        for (const char *p = pattern; *p; p++)
            if (*p >= 'A' && *p <= 'Z') { icase = 0; break; }   /* meant it */
    }
    if (icase)
        for (i64 c = 'A'; c <= 'Z'; c++) m->fold[c] = (u8)(c + 32);

    const char *e = pattern;
    while (*e && *e != '*' && *e != '?' && *e != '[') e++;
    m->pfxl = e - pattern;

    const char *last_star = 0;
    for (const char *p = pattern; *p; p++)
        if (*p == '*') last_star = p;
    if (last_star && last_star[1] && !has_meta(last_star + 1)) {
        m->sfx = last_star + 1;
        m->sfxl = len_of(m->sfx);
    }
}

static i64 match_substr(const NameMatcher *m, const char *name, i64 nlen) {
    const u8 *fold = m->fold;
    const char *pat = m->pat;
    u8 p0 = fold[(u8)pat[0]];
    const char *last = name + nlen - m->plen;   /* no room to match past here */
    for (const char *h = name; h <= last; h++) {
        if (fold[(u8)*h] != p0) continue;
        const char *a = h + 1, *b = pat + 1;
        while (*b && fold[(u8)*a] == fold[(u8)*b]) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static i64 match_class(const u8 *fold, const char *cp, const char **end, char c) {
    int neg = 0;
    if (*cp == '!') { neg = 1; cp++; }
    i64 ok = 0;
    u8 fc = fold[(u8)c];
    const char *p = cp;
    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            if (fc >= fold[(u8)*p] && fc <= fold[(u8)p[2]]) ok = 1;
            p += 3;
        } else {
            if (fc == fold[(u8)*p]) ok = 1;
            p++;
        }
    }
    if (*p == ']') p++;
    *end = p;
    return neg ? !ok : ok;
}

/* One backtrack point instead of recursion at every '*'. The recursive version
   was exponential: 'a*a*a*a*a*a*a*a*a*b*' against a long name of a's ran for
   over 30 seconds, where find answers in two milliseconds. */
static i64 glob_impl(const u8 *fold, const char *p, const char *n) {
    const char *star = 0, *retry = 0;

    while (*n) {
        const char *end;
        if (*p == '?') { p++; n++; continue; }
        if (*p == '[' && match_class(fold, p + 1, &end, *n)) { p = end; n++; continue; }
        if (*p && *p != '*' && *p != '[' && fold[(u8)*p] == fold[(u8)*n]) {
            p++; n++;
            continue;
        }
        if (*p == '*') { star = p++; retry = n; continue; }
        if (star) { p = star + 1; n = ++retry; continue; }
        return 0;
    }

    while (*p == '*') p++;
    return *p == 0;
}

i64 name_match(const NameMatcher *m, const char *name, i64 nlen) {
    if (m->plen == 0) return 1;
    if (nlen < m->plen && !m->meta) return 0;   /* too short either way */

    if (m->substr) return match_substr(m, name, nlen);

    const u8 *fold = m->fold;

    if (!m->meta) {
        for (i64 i = 0; i < m->plen; i++)
            if (fold[(u8)name[i]] != fold[(u8)m->pat[i]]) return 0;
        return nlen == m->plen;
    }

    if (m->pfxl) {
        if (nlen < m->pfxl) return 0;
        for (i64 i = 0; i < m->pfxl; i++)
            if (fold[(u8)name[i]] != fold[(u8)m->pat[i]]) return 0;
    }

    if (m->sfx) {
        if (nlen < m->sfxl) return 0;
        for (i64 i = 0; i < m->sfxl; i++)
            if (fold[(u8)name[nlen - m->sfxl + i]] != fold[(u8)m->sfx[i]]) return 0;
    }

    return glob_impl(fold, m->pat, name);
}
