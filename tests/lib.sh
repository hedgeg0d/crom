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
