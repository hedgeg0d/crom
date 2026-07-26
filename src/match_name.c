#include "match_name.h"

static i64 has_meta(const char *p) {
    for (; *p; p++) {
        if (*p == '*' || *p == '?' || *p == '[') return 1;
    }
    return 0;
}

static i64 match_class(const char *class_pat, const char **end, char c) {
    int negate = 0;
    if (*class_pat == '!') { negate = 1; class_pat++; }

    i64 matched = 0;
    const char *p = class_pat;
    while (*p && *p != ']') {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            if (c >= *p && c <= p[2]) matched = 1;
            p += 3;
        } else {
            if (c == *p) matched = 1;
            p++;
        }
    }
    if (*p == ']') p++;
    *end = p;
    return negate ? !matched : matched;
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
    if (!has_meta(pattern)) {
        const char *a = pattern, *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        return (*a == 0 && (*b == 0 || *b == '/'));
    }
    return glob_impl(pattern, name);
}
