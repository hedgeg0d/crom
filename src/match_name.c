#include "match_name.h"

static i64 has_meta(const char *p) {
    for (; *p; p++) {
        if (*p == '*' || *p == '?' || *p == '[') return 1;
    }
    return 0;
}

static i64 match_class(const char *cp, const char **end, char c) {
    int neg = 0;
    if (*cp == '!') { neg = 1; cp++; }
    i64 ok = 0;
    const char *p = cp;
    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            if (c >= *p && c <= p[2]) ok = 1;
            p += 3;
        } else {
            if (c == *p) ok = 1;
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
        if (*p != *n) return 0;
        p++; n++;
    }
    return *n == 0 || *n == '/';
}

i64 match_glob(const char *pattern, const char *name) {
    const char *a = pattern, *b = name;
    while (*a && *b && *a == *b) { a++; b++; }

    if (!has_meta(pattern))
        return (*a == 0 && (*b == 0 || *b == '/'));

    i64 nl = 0, pl = 0;
    while (name[nl]) nl++;
    while (pattern[pl]) pl++;

    const char *pfx_end = pattern;
    while (*pfx_end && *pfx_end != '*' && *pfx_end != '?' && *pfx_end != '[')
        pfx_end++;
    i64 pfxl = pfx_end - pattern;
    if (pfxl > 0) {
        if (nl < pfxl) return 0;
        for (i64 i = 0; i < pfxl; i++)
            if (name[i] != pattern[i]) return 0;
    }

    const char *last_star = 0;
    for (const char *p = pattern; *p; p++)
        if (*p == '*') last_star = p;

    if (last_star && last_star[1] && !has_meta(last_star + 1)) {
        const char *sfx = last_star + 1;
        i64 sfxl = 0;
        while (sfx[sfxl]) sfxl++;
        if (nl < sfxl) return 0;
        for (i64 i = 0; i < sfxl; i++)
            if (name[nl - sfxl + i] != sfx[i]) return 0;
    }

    return glob_impl(pattern, name);
}
