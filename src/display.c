#include "display.h"
#include "syscalls.h"
#include "util.h"

static const char *FRAMES[] = {
    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
    "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
    "\xe2\xa0\x87", "\xe2\xa0\x8f"
};
static const i64 NFRAMES = 10;

static i64 g_start_ns;
static volatile i64 g_last_update;

static i64 fmt_num(char *b, i64 n) {
    char t[32];
    if (n == 0) { b[0] = '0'; return 1; }
    i64 i = 0;
    while (n > 0) { t[i++] = '0' + (char)(n % 10); n /= 10; }
    for (i64 j = 0; j < i; j++) b[j] = t[i - 1 - j];
    return i;
}

static i64 fmt_time(char *b, i64 ms) {
    i64 sec = ms / 1000;
    i64 min = sec / 60;
    sec %= 60;
    i64 pos = 0;
    if (min < 10) { b[pos++] = ' '; pos += fmt_num(b + pos, min); }
    else pos += fmt_num(b + pos, min);
    b[pos++] = ':';
    if (sec < 10) b[pos++] = '0';
    pos += fmt_num(b + pos, sec);
    return pos;
}

void display_init(void) {
    struct { i64 sec; i64 nsec; } ts;
    syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts);
    g_start_ns = ts.sec * 1000000000L + ts.nsec;
    g_last_update = 0;
}

void display_update(i64 dirs, i64 files, i64 matches) {
    struct { i64 sec; i64 nsec; } ts;
    syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts);
    i64 now_ns = ts.sec * 1000000000L + ts.nsec;
    i64 elapsed_ms = (now_ns - g_start_ns) / 1000000;

    i64 frame = ((unsigned long)files * 37) % NFRAMES;

    char buf[128];
    i64 pos = 0;

    buf[pos++] = '\r';
    buf[pos++] = '\033';
    buf[pos++] = '[';
    buf[pos++] = 'K';
    buf[pos++] = ' ';

    pos += fmt_num(buf + pos, dirs);
    buf[pos++] = 'd';
    buf[pos++] = ' ';
    pos += fmt_num(buf + pos, files);
    buf[pos++] = 'f';
    buf[pos++] = ' ';

    if (matches > 0) {
        pos += fmt_num(buf + pos, matches);
        buf[pos++] = 'm';
        buf[pos++] = ' ';
    }

    pos += fmt_time(buf + pos, elapsed_ms);
    buf[pos++] = ' ';

    const char *sp = FRAMES[frame];
    for (i64 i = 0; sp[i]; i++) buf[pos++] = sp[i];

    write_all(STDERR_FILENO, buf, pos);
}

void display_done(i64 dirs, i64 files, i64 matches, i64 elapsed_us) {
    (void)elapsed_us;

    struct { i64 sec; i64 nsec; } ts;
    syscall2(SYS_clock_gettime, CLOCK_MONOTONIC, (long)&ts);
    i64 now_ns = ts.sec * 1000000000L + ts.nsec;
    i64 ms = (now_ns - g_start_ns) / 1000000;

    char buf[128];
    i64 pos = 0;
    buf[pos++] = '\r';
    buf[pos++] = '\033';
    buf[pos++] = '[';
    buf[pos++] = 'K';

    pos += fmt_num(buf + pos, dirs);
    buf[pos++] = 'd';
    buf[pos++] = ' ';
    pos += fmt_num(buf + pos, files);
    buf[pos++] = 'f';
    buf[pos++] = ' ';
    pos += fmt_num(buf + pos, matches);
    buf[pos++] = 'm';
    buf[pos++] = ' ';
    buf[pos++] = 'i';
    buf[pos++] = 'n';
    buf[pos++] = ' ';
    pos += fmt_time(buf + pos, ms);

    buf[pos++] = '\n';
    write_all(STDERR_FILENO, buf, pos);
}
