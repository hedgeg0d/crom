#!/usr/bin/env bash
# Benchmark for crom on a generated tree; the tree is removed on exit.
#
#   tests/bench.sh [--compare] [--scale N] [--runs N] [--keep]
#
#   --compare   also time find/grep/rg on the same tree
#   --scale N   tree size multiplier (default 1 ~ 6000 files, 48 MB)
#   --runs N    timed runs per case, median reported (default 5)
#   --keep      leave the tree in place and print its path

. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

COMPARE=0; SCALE=1; RUNS=5; KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --compare) COMPARE=1 ;;
        --scale)   SCALE=${2:?}; shift ;;
        --runs)    RUNS=${2:?}; shift ;;
        --keep)    KEEP=1 ;;
        -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done
[ -x "$CROM" ] || die "no binary at $CROM (run make first)"
[ "$KEEP" = 1 ] && trap - EXIT INT TERM

HAVE_RG=0; command -v rg >/dev/null 2>&1 && HAVE_RG=1
FD=""; for c in fd fdfind; do command -v "$c" >/dev/null 2>&1 && { FD=$c; break; }; done
# uutils findutils, if installed outside PATH so it cannot shadow GNU find
RFIND=""
for c in rfind "$HOME/.local/opt/uufind/bin/find"; do
    if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then RFIND=$c; break; fi
done

printf '%sgenerating tree (scale %s)...%s\n' "$C_DIM" "$SCALE" "$C_OFF"
make_fixture bench "$SCALE"
F=$FIXTURE
MB=$(awk "BEGIN{printf \"%.1f\", $B_BYTES/1048576}")
FSTYPE=$(df -T "$F" 2>/dev/null | awk 'NR==2{print $2}')
printf '%s%s files, %s dirs, %s MB%s  %s%s (%s)%s\n' \
    "$C_HEAD" "$B_FILES" "$B_DIRS" "$MB" "$C_OFF" "$C_DIM" "$F" "${FSTYPE:-?}" "$C_OFF"

# Pull everything into page cache first: otherwise the first tool measured
# pays for the disk and the comparison is meaningless.
find "$F" -type f -print0 | xargs -0 cat >/dev/null 2>&1

# --------------------------------------------------------------- comparison

# Verify tools agree before comparing their speed; a faster tool that returns a
# different set is not faster at the same job.
WARN=()
verify() {
    local label=$1 a=$2 b=$3
    [ "$a" = "$b" ] && return 0
    WARN+=("$label returned $b lines, crom $a -- timings not comparable")
    return 1
}
show_warnings() {
    [ ${#WARN[@]} -eq 0 ] && return 0
    printf '\n'
    local w
    for w in "${WARN[@]}"; do printf '  %swarning:%s %s\n' "$C_BAD" "$C_OFF" "$w"; done
}

# One row under construction; cell appends to it, tbl_row commits it.
ROW=()
cell() { if [ "$1" = "-" ]; then ROW+=("-"); else ROW+=("${1}ms"); fi; }

# ------------------------------------------------------------ by filename

head_ "filename search (median of $RUNS runs)"
NAME_COLS=(case crom)
[ "$COMPARE" = 1 ] && NAME_COLS+=(find find-rs fd)
tbl_reset
tbl_row "${NAME_COLS[@]}"

name_case() {                       # <label> <crom args...> -- reference sets
    local label=$1 glob=$2
    local c_n; c_n=$(crom -n "$glob" "$F" | wc -l)
    ROW=("$label")
    cell "$(median_ms "$RUNS" "$CROM" --no-config -n "$glob" "$F")"
    if [ "$COMPARE" = 1 ]; then
        local fargs=(-type f); [ "$glob" != '*' ] && fargs+=(-name "$glob")
        verify "find"  "$c_n" "$(command find "$F" "${fargs[@]}" | wc -l)"
        cell "$(median_ms "$RUNS" command find "$F" "${fargs[@]}")"
        if [ -n "$RFIND" ]; then
            verify "find-rs" "$c_n" "$("$RFIND" "$F" "${fargs[@]}" 2>/dev/null | wc -l)"
            cell "$(median_ms "$RUNS" "$RFIND" "$F" "${fargs[@]}")"
        else cell "-"; fi
        if [ -n "$FD" ]; then
            # -g: fd defaults to regex. --type f: fd also lists dirs and
            # symlinks. --hidden --no-ignore: match crom's default view.
            local fdargs=(--type f --hidden --no-ignore -g "$glob")
            verify "$FD" "$c_n" "$("$FD" "${fdargs[@]}" "$F" 2>/dev/null | wc -l)"
            cell "$(median_ms "$RUNS" "$FD" "${fdargs[@]}" "$F")"
        else cell "-"; fi
    fi
    tbl_row "${ROW[@]}"
}
# The default reading of a bare pattern: a case-folded substring of the name,
# which is also fd's native mode -- so here fd runs without -g.
word_case() {
    local label=$1 word=$2
    local c_n; c_n=$(crom "$word" "$F" | wc -l)
    ROW=("$label")
    cell "$(median_ms "$RUNS" "$CROM" --no-config "$word" "$F")"
    if [ "$COMPARE" = 1 ]; then
        local fargs=(-type f -iname "*$word*")
        verify "find"  "$c_n" "$(command find "$F" "${fargs[@]}" | wc -l)"
        cell "$(median_ms "$RUNS" command find "$F" "${fargs[@]}")"
        if [ -n "$RFIND" ]; then
            verify "find-rs" "$c_n" "$("$RFIND" "$F" "${fargs[@]}" 2>/dev/null | wc -l)"
            cell "$(median_ms "$RUNS" "$RFIND" "$F" "${fargs[@]}")"
        else cell "-"; fi
        if [ -n "$FD" ]; then
            local fdargs=(--type f --hidden --no-ignore "$word")
            verify "$FD" "$c_n" "$("$FD" "${fdargs[@]}" "$F" 2>/dev/null | wc -l)"
            cell "$(median_ms "$RUNS" "$FD" "${fdargs[@]}" "$F")"
        else cell "-"; fi
    fi
    tbl_row "${ROW[@]}"
}

name_case "glob  *.txt" '*.txt'
name_case "every file"  '*'
word_case "bare word  f01" 'f01'
tbl_flush best

# ------------------------------------------------------------- by content

head_ "content search (median of $RUNS runs)"
CONTENT_COLS=(case crom)
[ "$COMPARE" = 1 ] && CONTENT_COLS+=(grep rg)
tbl_reset
tbl_row "${CONTENT_COLS[@]}"

for spec in "COMMON_TOKEN:40% of files" \
            "RARE_TOKEN_XYZ:rare token" \
            "NO_SUCH_TOKEN_ZZ:no match (reads all)"; do
    tok=${spec%%:*}; label=${spec#*:}
    c_n=$(crom -c "$tok" "$F" | wc -l)
    ROW=("$label")
    cell "$(median_ms "$RUNS" "$CROM" --no-config -c "$tok" "$F")"
    if [ "$COMPARE" = 1 ]; then
        verify "grep" "$c_n" "$(GREP -rIl "$tok" "$F" 2>/dev/null | wc -l)"
        cell "$(median_ms "$RUNS" command grep -rIl "$tok" "$F")"
        if [ "$HAVE_RG" = 1 ]; then
            verify "rg" "$c_n" "$(rg -l --no-ignore --no-messages "$tok" "$F" 2>/dev/null | wc -l)"
            cell "$(median_ms "$RUNS" rg -l --no-ignore --no-messages "$tok" "$F")"
        else cell "-"; fi
    fi
    tbl_row "${ROW[@]}"
done
tbl_flush best

# --------------------------------------------------------------- scaling

head_ "thread scaling (content, no match)"
NPROC=$(nproc 2>/dev/null || echo 8)
base=""
tbl_reset
tbl_row "threads" "time" "speedup"
for j in 1 2 4 8 16; do
    [ "$j" -gt $((NPROC * 2)) ] && continue
    t=$(median_ms "$RUNS" "$CROM" --no-config -j "$j" -c NO_SUCH_TOKEN_ZZ "$F")
    [ -z "$base" ] && base=$t
    tbl_row "-j $j" "${t}ms" "x$(awk "BEGIN{printf \"%.2f\", $base/$t}")"
done
tbl_flush

show_warnings

head_ "startup overhead"
mkdir -p "$F/.empty"
tbl_reset
tbl_row "case" "time"
tbl_row "empty directory" "$(median_ms 40 "$CROM" --no-config -c x "$F/.empty")ms"
tbl_flush

[ "$KEEP" = 1 ] && printf '\n%stree kept: %s%s\n' "$C_DIM" "$F" "$C_OFF"
printf '\n'
