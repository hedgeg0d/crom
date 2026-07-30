# shellcheck shell=bash
# Shared helpers for tests/run.sh and tests/bench.sh.

set -uo pipefail
export LC_ALL=C          # keep sort order stable across locales

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CROM=${CROM:-$ROOT/build/crom}

# Ignore any ~/.cromrc: a stray --bar there would change what we measure.
# CROM_EXTRA lets a caller run the whole suite with extra flags, e.g. -j 1.
CROM_ARGS=(--no-config ${CROM_EXTRA:-})

if [ -t 1 ]; then
    C_OK=$'\033[32m'; C_BAD=$'\033[31m'; C_HEAD=$'\033[1m'
    C_DIM=$'\033[2m'; C_OFF=$'\033[0m'
else
    C_OK=; C_BAD=; C_HEAD=; C_DIM=; C_OFF=
fi

die()  { printf '%serror:%s %s\n' "$C_BAD" "$C_OFF" "$*" >&2; exit 2; }
head_() { printf '\n%s%s%s\n' "$C_HEAD" "$*" "$C_OFF"; }

need() { command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1"; }

# `grep` is a shell function in some interactive setups; always take the binary.
GREP() { command grep "$@"; }

crom() { "$CROM" "${CROM_ARGS[@]}" "$@"; }

# ------------------------------------------------------------------ tables

# Rows are collected first so every column can be sized to its widest cell.
# tbl_row takes one argument per column; the first row is the header.
TBL=()
tbl_reset() { TBL=(); }
tbl_row() { local IFS=$'\x1f'; TBL+=("$*"); }

_rep() { local n=$1 c=$2 out=; while [ "$n" -gt 0 ]; do out+=$c; n=$((n-1)); done; printf '%s' "$out"; }

# tbl_flush [best]   "best" paints the lowest number in each data row green
tbl_flush() {
    local best=${1:-}
    local -a w=() cells=()
    local r i ncol=0

    for r in "${TBL[@]}"; do
        IFS=$'\x1f' read -r -a cells <<<"$r"
        [ ${#cells[@]} -gt "$ncol" ] && ncol=${#cells[@]}
        for ((i = 0; i < ${#cells[@]}; i++)); do
            [ ${#cells[i]} -gt "${w[i]:-0}" ] && w[i]=${#cells[i]}
        done
    done
    [ "$ncol" -eq 0 ] && return 0

    local top="" mid="" bot="" bar
    for ((i = 0; i < ncol; i++)); do
        bar=$(_rep $((w[i] + 2)) '─')
        if [ "$i" -eq 0 ]; then top="┌$bar"; mid="├$bar"; bot="└$bar"
        else top+="┬$bar"; mid+="┼$bar"; bot+="┴$bar"; fi
    done
    top+="┐"; mid+="┤"; bot+="┘"

    local n win pad
    printf '  %s\n' "$top"
    for ((n = 0; n < ${#TBL[@]}; n++)); do
        IFS=$'\x1f' read -r -a cells <<<"${TBL[n]}"
        win=-1
        # With a single data column the winner is whoever is there: pointless.
        if [ -n "$best" ] && [ "$n" -gt 0 ] && [ "$ncol" -gt 2 ]; then
            win=$(printf '%s\n' "${cells[@]:1}" | awk '
                { v = $0; sub(/ms$/, "", v)
                  if (v ~ /^[0-9.]+$/ && (b == "" || v + 0 < b)) { b = v + 0; k = NR } }
                END { print (k ? k : 0) }')
        fi
        printf '  │'
        for ((i = 0; i < ncol; i++)); do
            if [ "$i" -eq 0 ]; then pad=$(printf '%-*s' "${w[i]}" "${cells[i]:-}")
            else pad=$(printf '%*s' "${w[i]}" "${cells[i]:-}"); fi
            if [ "$i" -eq "$win" ]; then printf ' %s%s%s │' "$C_OK" "$pad" "$C_OFF"
            elif [ "$n" -eq 0 ]; then printf ' %s%s%s │' "$C_HEAD" "$pad" "$C_OFF"
            else printf ' %s │' "$pad"; fi
        done
        printf '\n'
        [ "$n" -eq 0 ] && printf '  %s\n' "$mid"
    done
    printf '  %s\n' "$bot"
    tbl_reset
}

# ---------------------------------------------------------------- fixtures

# Temp tree is created under TMPDIR and removed on any exit, including Ctrl-C.
FIXTURE=""
cleanup() {
    [ -n "$FIXTURE" ] || return 0
    [ -d "$FIXTURE" ] || return 0
    case "$FIXTURE" in
        /tmp/*|/var/tmp/*|"${TMPDIR:-/nonexistent}"/*) chmod -R u+rwX "$FIXTURE" 2>/dev/null; rm -rf "$FIXTURE" ;;
        *) printf 'refusing to remove unexpected path: %s\n' "$FIXTURE" >&2 ;;
    esac
    rm -f "$FIXTURE.meta"
    FIXTURE=""
}
trap cleanup EXIT INT TERM

# make_fixture <kind> [scale]   -> sets FIXTURE and sources FIXTURE/.meta
# kind: correctness | bench
make_fixture() {
    need python3
    FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/crom-$1-XXXXXX") || die "mktemp failed"
    META="$FIXTURE.meta"
    python3 "$ROOT/tests/gen_fixture.py" "$1" "$FIXTURE" "${2:-1}" >"$META" \
        || die "fixture generation failed"
    # shellcheck disable=SC1091
    . "$META"
}

# ---------------------------------------------------------------- timing

# Microsecond clock. $EPOCHREALTIME is a bash builtin, so it costs nothing;
# `date +%s%N` forks, which added 1-2 ms to every sample and swamped the
# millisecond-scale cases we care about.
if [ -n "${EPOCHREALTIME:-}" ]; then
    now_us() { local e=$EPOCHREALTIME; printf '%s' "${e/./}"; }
else
    now_us() { date +%s%6N; }
fi

# median_ms <runs> <command...>  -> median wall time in ms
#
# Median rather than fastest-run: the minimum reports the one lucky pass where
# nothing else touched the CPU, which flatters whichever tool happens to run
# first. The median is what the machine actually does under normal noise.
median_ms() {
    local runs=$1; shift
    local -a s=()
    local t0 t1 i j tmp
    "$@" >/dev/null 2>&1                       # warm page cache / dentries
    for ((i = 0; i < runs; i++)); do
        t0=$(now_us)
        "$@" >/dev/null 2>&1
        t1=$(now_us)
        s+=( $((t1 - t0)) )
    done

    # insertion sort: the sample count is tiny and this avoids forking sort
    for ((i = 1; i < ${#s[@]}; i++)); do
        tmp=${s[i]}
        for ((j = i - 1; j >= 0 && s[j] > tmp; j--)); do s[j+1]=${s[j]}; done
        s[j+1]=$tmp
    done

    local n=${#s[@]} mid med
    mid=$((n / 2))
    if (( n % 2 )); then med=${s[mid]}
    else med=$(( (s[mid-1] + s[mid]) / 2 )); fi

    printf '%d.%01d' $((med / 1000)) $(((med % 1000) / 100))
}
