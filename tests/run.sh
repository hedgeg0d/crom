#!/usr/bin/env bash
# Functional tests for crom.
#
#   tests/run.sh [--compare] [--keep] [-v]
#
#   --compare  additionally cross-check crom against find/grep/rg
#   --keep     leave the fixture tree in place and print its path
#   -v         print every test, not just failures

. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

COMPARE=0; KEEP=0; VERBOSE=0
for a in "$@"; do
    case "$a" in
        --compare) COMPARE=1 ;;
        --keep)    KEEP=1 ;;
        -v)        VERBOSE=1 ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) die "unknown option: $a" ;;
    esac
done
[ -x "$CROM" ] || die "no binary at $CROM (run make first)"
[ "$KEEP" = 1 ] && trap - EXIT INT TERM

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); [ "$VERBOSE" = 1 ] && printf '  %sok%s   %s\n' "$C_OK" "$C_OFF" "$1"; return 0; }
bad()  { FAIL=$((FAIL+1)); printf '  %sFAIL%s %s\n       want: %s\n       got:  %s\n' \
         "$C_BAD" "$C_OFF" "$1" "$2" "$3"; }

# is <name> <expected> <command...>
is() { local n=$1 want=$2; shift 2
       local got; got=$("$@" 2>/dev/null)
       [ "$got" = "$want" ] && ok "$n" || bad "$n" "$want" "$got"; }

# count <name> <expected> <command...>   (lines on stdout)
count() { local n=$1 want=$2; shift 2
          local got; got=$("$@" 2>/dev/null | wc -l)
          [ "$got" -eq "$want" ] && ok "$n" || bad "$n" "$want lines" "$got lines"; }

# rc <name> <expected> <command...>
rc() { local n=$1 want=$2; shift 2
       "$@" >/dev/null 2>&1; local got=$?
       [ "$got" -eq "$want" ] && ok "$n" || bad "$n" "exit $want" "exit $got"; }

make_fixture correctness
F=$FIXTURE

head_ "content search  ($CROM)"
count "plain text matches"      "$N_TEXT_HIT"   crom -c "$NEEDLE" "$F/text"
count "chunk-boundary needle"   "$N_CHUNK_HIT"  crom -c "$NEEDLE" "$F/chunk"
count "short needle (SIMD)"     "$N_SHORT_HIT"  crom -c "$SHORT"  "$F/chunk"
count "binaries skipped"        0               crom -c "$NEEDLE" "$F/binary"
count "binaries with -a"        "$N_BIN_HIT"    crom -a -c "$NEEDLE" "$F/binary"
count "NUL past 4K probe"       "$N_LATENUL_HIT" crom -c "$NEEDLE" "$F/lateNUL"
count "deep nesting"            "$N_DEEP_HIT"   crom -c "$NEEDLE" "$F/deep"
count "no match"                0               crom -c "NOSUCHNEEDLE" "$F/text"
count "empty dir"               0               crom -c "$NEEDLE" "$F/empty/dir"

head_ "determinism across -j"
BASE=$(crom -c "$NEEDLE" "$F/text" 2>/dev/null | sort)
for j in 1 2 3 5 8 16 32; do
    got=$(crom -j "$j" -c "$NEEDLE" "$F/text" 2>/dev/null | sort)
    [ "$got" = "$BASE" ] && ok "-j $j identical" || bad "-j $j identical" "same set" "differs"
done
same=1
for _ in $(seq 1 10); do
    [ "$(crom -c "$NEEDLE" "$F/text" 2>/dev/null | sort)" = "$BASE" ] || same=0
done
[ "$same" = 1 ] && ok "10 repeat runs stable" || bad "10 repeat runs stable" "same" "varies"

head_ "filters"
count "-n glob"        "$N_TEXT_TOTAL" crom -n '*.txt' "$F/text"
count "-t f"           "$N_TEXT_TOTAL" crom -t f -n '*.txt' "$F/text"
count "-t d"           5               crom -t d -n 'sub*' "$F/text"
count "-t l"           "$N_SYMLINK"    crom -t l -n '*' "$F"
count "-s +100000"     "$N_SIZE_BIG"   crom -n '*.dat' -s +100000 "$F/sizes"
count "--depth 0"      0               crom -n '*.txt' --depth 0 "$F/text"
count "-e exec"        "$N_TEXT_HIT"   crom -c "$NEEDLE" -e 'echo {}' "$F/text"

head_ "name matching"
count "bare word is a substring"  "$N_PDF_ANY"    crom pdf "$F/names"
count "capital makes it strict"   "$N_PDF_STRICT" crom PDF "$F/names"
count "-i overrides the capital"  "$N_PDF_ANY"    crom -i PDF "$F/names"
is "--case-sensitive overrides"   "paper.pdf"     bash -c "'$CROM' --no-config --case-sensitive pdf '$F/names' | sed 's|.*/||'"
count "substring hits mid-name"   "$N_FILE_ANY"   crom file "$F/names"
count "-g anchors to whole name"  "$N_FILE_WHOLE" crom -g file "$F/names"
count "-n takes a substring too"  "$N_FILE_ANY"   crom -n file "$F/names"
count "glob spans the whole name" "$N_GLOB_PDF"   crom '*.pdf' "$F/names"
count "glob folds case as well"   "$N_GLOB_PDF_CS" crom --case-sensitive '*.pdf' "$F/names"
count "glob capital stays strict" "$N_MAKE_CAP"   crom 'M*' "$F/names"
count "? still matches one char"  1               crom '?ile' "$F/names"
count "[class] still works"       1               crom '[mM]akefile' "$F/names"
count "-g with a glob is a glob"  "$N_GLOB_PDF"   crom -g '*.pdf' "$F/names"
count "no match"                  0               crom zzznope "$F/names"

head_ "gitignore"
count "inherited into subdirs" "$N_GIT_VISIBLE" crom -c "$NEEDLE" "$F/git"
count "--no-ignore"            "$N_GIT_ALL"     crom --no-ignore -c "$NEEDLE" "$F/git"
count "nested negation"        "$N_GIT2_VISIBLE" crom -c "$NEEDLE" "$F/git2"
count "parent rule survives child .gitignore" "$N_GIT3_VISIBLE" crom -c "$NEEDLE" "$F/git3"
is "built-in ignore (build/)" "0" bash -c "'$CROM' --no-config -c '$NEEDLE' '$F' | command grep -c '/build/'"
is "but explicit root is scanned" "1" bash -c "'$CROM' --no-config -c '$NEEDLE' '$F/build' | wc -l"

head_ "output format"
is "--json objects"  "$N_TEXT_HIT" bash -c "'$CROM' --no-config --json -c '$NEEDLE' '$F/text' | command grep -c '^{\"path\"'"
is    "no ANSI in results" "0" bash -c "'$CROM' --no-config --color always -n '*.txt' '$F/text' | command grep -c \$'\033'"
NULS=$(crom -0 -n '*.txt' "$F/weird" 2>/dev/null | tr -dc '\0' | wc -c)
[ "$NULS" -eq "$N_WEIRD" ] && ok "-0 NUL separators" || bad "-0 NUL separators" "$N_WEIRD" "$NULS"
INTACT=$(crom -0 -n '*.txt' "$F/weird" 2>/dev/null | xargs -0 -n1 sh -c 'test -f "$1" && echo y' _ | wc -l)
[ "$INTACT" -eq "$N_WEIRD" ] && ok "-0 survives odd names" || bad "-0 survives odd names" "$N_WEIRD" "$INTACT"
count "without -0: newline sep" 0 bash -c "'$CROM' --no-config -n '*.txt' '$F/text' | tr -dc '\0' | wc -c | command grep -v '^0$'"

head_ "argument handling"
rc "unknown long flag"    2 crom --bogus -c "$NEEDLE" "$F/text"
rc "unknown short flag"   2 crom -Z -c "$NEEDLE" "$F/text"
rc "typo in known flag"   2 crom --jsom -c "$NEEDLE" "$F/text"
rc "bare dash"            2 crom - -c "$NEEDLE" "$F/text"
is "names the bad option" "1" bash -c "'$CROM' --no-config --jsom -c x '$F/text' 2>&1 >/dev/null | command grep -c \"unrecognized option '--jsom'\""
is "unknown flag says nothing on stdout" "0" bash -c "'$CROM' --no-config --bogus -c x '$F/text' 2>/dev/null | wc -c"
rc "--help wins over typo"  0 crom --help --bogus
rc "-V wins over typo"      0 crom -V --bogus
count "-- reaches -dashdir"  "$N_DASHDIR" bash -c "cd '$F' && '$CROM' --no-config -c '$NEEDLE' -- -dashdir"
rc "without -- it is an error" 2 bash -c "cd '$F' && '$CROM' --no-config -c '$NEEDLE' -dashdir"
count "glob + path still work" "$N_TEXT_TOTAL" crom '*.txt' "$F/text"

head_ "exit codes"
rc "0 when found"        0 crom -c "$NEEDLE" "$F/text"
rc "1 when nothing"      1 crom -c "NOSUCHNEEDLE" "$F/text"
rc "2 on missing path"   2 crom -n '*' "$F/does-not-exist"
[ "${HAVE_NOPERM:-0}" = 1 ] && rc "2 on unreadable root" 2 crom -n '*' "$F/noperm"
rc "0 for --help"        0 crom --help
rc "0 for -V"            0 crom -V

head_ "cli hygiene"
is "--help does not scan" "0" bash -c "'$CROM' --no-config --help | command grep -c '^/'"
count "-V prints one line"    1 crom -V
is "help has no ANSI in pipe" "0" bash -c "'$CROM' --no-config --help | command grep -c \$'\033'"
ansi=$(crom --color always --help | command grep -c $'\033')
[ "$ansi" -gt 0 ] && ok "--color always keeps ANSI" || bad "--color always keeps ANSI" ">0" "$ansi"
is "--color never strips"      "0" bash -c "'$CROM' --no-config --color never --help | command grep -c \$'\033'"
is "help text identical either way" "same" bash -c "
  diff <('$CROM' --no-config --color always --help | sed 's/\x1b\[[0-9;]*m//g') \
       <('$CROM' --no-config --color never --help) >/dev/null && echo same || echo differs"
is "no bar when stderr is not a tty" "0" bash -c "'$CROM' --no-config --bar -c '$NEEDLE' '$F/text' 2>&1 >/dev/null | wc -c"
is "stdout empty on error" "0" bash -c "'$CROM' --no-config -n '*' '$F/nope' 2>/dev/null | wc -c"
is "error message on stderr" "1" bash -c "'$CROM' --no-config -n '*' '$F/nope' 2>&1 >/dev/null | command grep -c 'cannot open'"

if [ "$COMPARE" = 1 ]; then
    head_ "cross-check against system tools"
    # text/ and chunk/ are pure text with no ignore files: all tools agree there
    for sub in text chunk; do
        want=$(GREP -rl "$NEEDLE" "$F/$sub" 2>/dev/null | sort)
        got=$(crom -c "$NEEDLE" "$F/$sub" 2>/dev/null | sort)
        [ "$got" = "$want" ] && ok "vs grep -rl ($sub)" \
            || bad "vs grep -rl ($sub)" "$(printf '%s' "$want" | wc -l) files" "$(printf '%s' "$got" | wc -l) files"
    done
    if command -v rg >/dev/null 2>&1; then
        want=$(rg -l --no-ignore --no-messages "$NEEDLE" "$F/text" 2>/dev/null | sort)
        got=$(crom -c "$NEEDLE" "$F/text" 2>/dev/null | sort)
        [ "$got" = "$want" ] && ok "vs rg -l" || bad "vs rg -l" "rg set" "crom set differs"
    else
        printf '  %sskip%s rg not installed\n' "$C_DIM" "$C_OFF"
    fi
    want=$(find "$F/text" -type f -name '*.txt' | sort)
    got=$(crom -n '*.txt' "$F/text" 2>/dev/null | sort)
    [ "$got" = "$want" ] && ok "vs find -name" || bad "vs find -name" "find set" "crom set differs"
    # binaries are where the tools legitimately differ; -a should match grep -a
    want=$(GREP -arl "$NEEDLE" "$F/binary" 2>/dev/null | sort)
    got=$(crom -a -c "$NEEDLE" "$F/binary" 2>/dev/null | sort)
    [ "$got" = "$want" ] && ok "vs grep -a (binaries)" || bad "vs grep -a (binaries)" "grep set" "crom set differs"
fi

printf '\n%s%d passed%s' "$C_OK" "$PASS" "$C_OFF"
[ "$FAIL" -gt 0 ] && printf ', %s%d failed%s' "$C_BAD" "$FAIL" "$C_OFF"
printf '\n'
[ "$KEEP" = 1 ] && printf '%sfixture kept: %s%s\n' "$C_DIM" "$FIXTURE" "$C_OFF"
[ "$FAIL" -eq 0 ]
