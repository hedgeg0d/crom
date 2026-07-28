#include "display.h"
#include "syscalls.h"
#include "util.h"
#include "atomics.h"

static const char *FRAMES[] = {
    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
    "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
    "\xe2\xa0\x87", "\xe2\xa0\x8f"
};
static const i64 NFRAMES = 10;

#define SPIN_MS 90

static i64 g_start_ns;
static volatile i64 g_drawn;      /* a bar line is currently on screen */

static i64 now_ms(void) {
    struct { i64 sec; i64 nsec; } ts;
    syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts);
    return (ts.sec * 1000000000L + ts.nsec - g_start_ns) / 1000000;
}

static i64 fmt_pad(char *b, i64 n, i64 width) {
    char t[24];
    i64 i = 0;
    if (n <= 0) t[i++] = '0';
    while (n > 0) { t[i++] = '0' + (char)(n % 10); n /= 10; }
    i64 pos = 0;
    for (i64 p = width - i; p > 0; p--) b[pos++] = ' ';
    while (i > 0) b[pos++] = t[--i];
    return pos;
}

static i64 fmt_time(char *b, i64 ms) {
    i64 sec = ms / 1000;
    i64 min = sec / 60;
    sec %= 60;
    i64 pos = fmt_pad(b, min, 2);
    b[pos++] = ':';
    b[pos++] = '0' + (char)(sec / 10);
    b[pos++] = '0' + (char)(sec % 10);
    return pos;
}

static i64 lit(char *b, i64 pos, const char *s) {
    while (*s) b[pos++] = *s++;
    return pos;
}

i64 display_init(void) {
    struct { i64 sec; i64 nsec; } ts;
    syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts);
    g_start_ns = ts.sec * 1000000000L + ts.nsec;
    g_drawn = 0;
    /* Escape sequences into a pipe or a file are just garbage. */
    return is_tty(STDERR_FILENO);
}

void display_clear(void) {
    if (!atomic_load(&g_drawn)) return;
    atomic_store(&g_drawn, 0);
    write_all(STDERR_FILENO, "\r\033[K", 4);
}

void display_update(i64 dirs, i64 files, i64 matches) {
    i64 ms = now_ms();
    char buf[256];
    i64 pos = 0;

    pos = lit(buf, pos, "\r\033[36m ");
    pos = lit(buf, pos, FRAMES[(ms / SPIN_MS) % NFRAMES]);
    pos = lit(buf, pos, "\033[0m  ");

    pos += fmt_pad(buf + pos, dirs, 6);
    pos = lit(buf, pos, "\033[2m dirs\033[0m  ");
    pos += fmt_pad(buf + pos, files, 7);
    pos = lit(buf, pos, "\033[2m files\033[0m  ");
    pos += fmt_pad(buf + pos, matches, 6);
    pos = lit(buf, pos, "\033[2m matches\033[0m  ");
    pos += fmt_time(buf + pos, ms);
    pos = lit(buf, pos, "\033[K");

    atomic_store(&g_drawn, 1);
    write_all(STDERR_FILENO, buf, pos);
}

void display_done(i64 dirs, i64 files, i64 matches, i64 elapsed_us) {
    (void)elapsed_us;
    i64 ms = now_ms();

    char buf[256];
    i64 pos = 0;
    pos = lit(buf, pos, "\r\033[K\033[1;32m");
    pos += fmt_pad(buf + pos, dirs, 1);
    pos = lit(buf, pos, "\033[0m dirs  \033[1;32m");
    pos += fmt_pad(buf + pos, files, 1);
    pos = lit(buf, pos, "\033[0m files  \033[1;32m");
    pos += fmt_pad(buf + pos, matches, 1);
    pos = lit(buf, pos, "\033[0m matches  \033[2m");
    pos += fmt_time(buf + pos, ms);
    pos = lit(buf, pos, "\033[0m\n");

    atomic_store(&g_drawn, 0);
    write_all(STDERR_FILENO, buf, pos);
}
