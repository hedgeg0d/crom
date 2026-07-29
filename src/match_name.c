#include "match_name.h"

/* Case folding is a table rather than a branch: for a case-sensitive search it
   is the identity map, so both modes run the same code and the comparison loops
   stay free of per-character tests. */
static u8 g_fold[256];

static const char *g_pat;
static i64 g_plen;
static i64 g_substr;        /* match anywhere in the name instead of all of it */
static i64 g_meta;          /* pattern holds * ? or [ */
static i64 g_pfxl;          /* literal run before the first metacharacter */
static const char *g_sfx;   /* literal tail after the last '*', or 0 */
static i64 g_sfxl;

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

/* Everything here used to be recomputed per candidate file: the matcher walked
   the pattern three times (metacharacter scan, literal prefix, last '*') for
   every name it was handed. */
void name_prepare(const char *pattern, i64 case_mode, i64 force_glob) {
    for (i64 i = 0; i < 256; i++) g_fold[i] = (u8)i;
    g_pat = pattern;
    g_plen = 0; g_substr = 0; g_meta = 0;
    g_pfxl = 0; g_sfx = 0; g_sfxl = 0;
    if (!pattern) return;

    g_plen = len_of(pattern);
    g_meta = has_meta(pattern);

    /* A bare word is what people type when they mean "name contains this";
       a pattern carrying * ? or [ is a glob over the whole name, as before. */
    g_substr = !force_glob && !g_meta;

    i64 icase;
    if (case_mode == CASE_SENSITIVE)   icase = 0;
    else if (case_mode == CASE_IGNORE) icase = 1;
    else {
        icase = 1;
        for (const char *p = pattern; *p; p++)
            if (*p >= 'A' && *p <= 'Z') { icase = 0; break; }   /* meant it */
    }
    if (icase)
        for (i64 c = 'A'; c <= 'Z'; c++) g_fold[c] = (u8)(c + 32);

    const char *e = pattern;
    while (*e && *e != '*' && *e != '?' && *e != '[') e++;
    g_pfxl = e - pattern;

    const char *last_star = 0;
    for (const char *p = pattern; *p; p++)
        if (*p == '*') last_star = p;
    if (last_star && last_star[1] && !has_meta(last_star + 1)) {
        g_sfx = last_star + 1;
        g_sfxl = len_of(g_sfx);
    }
}

static i64 match_substr(const char *name, i64 nlen) {
    const char *pat = g_pat;
    u8 p0 = g_fold[(u8)pat[0]];
    const char *last = name + nlen - g_plen;   /* no room to match past here */
    for (const char *h = name; h <= last; h++) {
        if (g_fold[(u8)*h] != p0) continue;
        const char *a = h + 1, *b = pat + 1;
        while (*b && g_fold[(u8)*a] == g_fold[(u8)*b]) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static i64 match_class(const char *cp, const char **end, char c) {
    int neg = 0;
    if (*cp == '!') { neg = 1; cp++; }
    i64 ok = 0;
    u8 fc = g_fold[(u8)c];
    const char *p = cp;
    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            if (fc >= g_fold[(u8)*p] && fc <= g_fold[(u8)p[2]]) ok = 1;
            p += 3;
        } else {
            if (fc == g_fold[(u8)*p]) ok = 1;
            p++;
        }
    }
    if (*p == ']') p++;
    *end = p;
    return neg ? !ok : ok;
}

static i64 glob_impl(const char *p, const char *n) {
    while (*p) {
        if (*p == '*') {
            do { p++; } while (*p == '*');
            if (*p == 0) return 1;
            for (; *n; n++) {
                if (glob_impl(p, n)) return 1;
            }
            return 0;
        }
        if (*p == '?') {
            if (*n == 0 || *n == '/') return 0;
            p++; n++;
            continue;
        }
        if (*p == '[') {
            const char *end;
            if (!match_class(p + 1, &end, *n)) return 0;
            if (*n == 0 || *n == '/') return 0;
            p = end; n++;
            continue;
        }
        if (g_fold[(u8)*p] != g_fold[(u8)*n]) return 0;
        p++; n++;
    }
    return *n == 0 || *n == '/';
}

i64 match_name(const char *name, i64 nlen) {
    if (g_plen == 0) return 1;
    if (nlen < g_plen && !g_meta) return 0;   /* too short either way */

    if (g_substr) return match_substr(name, nlen);

    if (!g_meta) {
        for (i64 i = 0; i < g_plen; i++)
            if (g_fold[(u8)name[i]] != g_fold[(u8)g_pat[i]]) return 0;
        return nlen == g_plen;
    }

    if (g_pfxl) {
        if (nlen < g_pfxl) return 0;
        for (i64 i = 0; i < g_pfxl; i++)
            if (g_fold[(u8)name[i]] != g_fold[(u8)g_pat[i]]) return 0;
    }

    if (g_sfx) {
        if (nlen < g_sfxl) return 0;
        for (i64 i = 0; i < g_sfxl; i++)
            if (g_fold[(u8)name[nlen - g_sfxl + i]] != g_fold[(u8)g_sfx[i]]) return 0;
    }

    return glob_impl(g_pat, name);
}
