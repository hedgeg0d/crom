# crom

A file hunter for Linux: finds files by name or by content, in parallel, with no
libc between it and the kernel. One static binary, about 35 KB.

```
crom pdf                  # every name containing "pdf", any case
crom '*.rs' src tests     # glob, two search paths
crom -c TODO              # files whose contents contain TODO
crom -n '*.rs' -c unsafe  # Rust files that mention unsafe
```

## Why

It is faster than the alternatives at both jobs, and it needs no runtime.
Measured on this machine (8 cores, `/usr/include`, 22032 files, best of 11 runs,
every tool given the flags that make it walk the same set):

| by name | | by content | |
|---|---|---|---|
| **crom** | **5.8 ms** | **crom** | **38.1 ms** |
| fd | 13.7 ms | rg | 43.3 ms |
| rg --files | 32.5 ms | grep -rl | 188.8 ms |
| find | 33.0 ms | | |
| uutils find | 51.9 ms | | |

`make bench-compare` reproduces this on a generated tree, checks that every tool
returned the same number of results, and says so if they did not.

## Install

From the AUR:

```
paru -S crom        # or: yay -S crom
```

From source (needs gcc, or clang with lld):

```
make
sudo make install           # PREFIX=/usr/local by default
```

Both routes build with `-march=native`, so the binary is tuned for the machine
that compiled it — which is the point of installing a source package. It may not
run on an older CPU. For a portable build:

```
make ARCH_FLAGS=-march=x86-64-v2       # from source
_native=0 makepkg -si                  # from the PKGBUILD
```

## Patterns

A bare word matches any name **containing** it. Add `* ? [` and the pattern
becomes a glob over the **whole** name:

```
crom report        # my-report-2026.pdf, REPORTS/, ...
crom '*.pdf'       # only names ending in .pdf
crom -g report     # only a file named exactly "report"
```

Case follows the pattern: the search folds case until the pattern itself carries
a capital. `crom pdf` finds `notes.PDF`, `crom PDF` does not. `-i` and
`--case-sensitive` override that, for names and for `-c` alike.

## Options

```
-n, --name <pat>        filename pattern
-g, --glob <pat>        match the whole name, not a part
-i, --ignore-case       fold case in names and content
    --case-sensitive    match as typed
-E, --exclude <glob>    skip names matching glob
-c, --content <text>    search file contents
-t, --type <f|d|l>      filter by type
-s, --size <[+-]N>      filter by size (k, m, g suffixes)
-H, --hidden            include dot files and dirs
-u, --unrestricted      --hidden --no-ignore
-L, --follow            follow symlinked directories
    --depth <N>         max recursion depth
    --max-results <N>   stop after N matches
-e, --exec <cmd> {}     run command per result
-j, --threads <N>       worker threads (default: one per core)
-a, --text              search binary files too
-0, --null              null-separated output
    --json              JSON output
    --bar               progress bar
-q, --no-bar            quiet, no progress bar
    --no-ignore         do not read .gitignore
    --no-messages       do not report unreadable paths
    --no-config         skip ~/.cromrc
    --color <when>      auto|always|never
```

By default crom skips dot entries, whatever `.gitignore` excludes, and a built-in
list (`.git`, `node_modules`, `__pycache__`, `build`, …). `-H`, `--no-ignore` and
`-u` turn those off.

## Config

`~/.cromrc` holds default flags, one per line or several to a line, `#` starts a
comment:

```
# always show the progress bar, never colour a pipe
--bar
--color auto
```

`--no-config` ignores it.

## Exit codes

The grep convention:

| code | meaning |
|---|---|
| 0 | something matched |
| 1 | nothing matched |
| 2 | a named path could not be opened, or results are incomplete |

Directories that could not be read during the walk are reported on stderr and do
**not** change the exit code — any large tree has some. `--no-messages` silences
them.

## Scripting

Output is one path per line, unquoted, so `crom '*.txt'` drops straight into
`$(...)`. For names that can contain anything:

```
crom -0 '*.txt' | xargs -0 rm
```

`-e` runs a shell command per result with `{}` replaced by the path. The path is
quoted, so a file named `a; rm -rf ~` is inert.

## Limits

* Linux on x86-64 only. It talks to the kernel directly, so there is no
  portability layer to carry it elsewhere.
* Paths are capped at 4096 bytes; anything longer is skipped and reported.
* Patterns are globs, not regular expressions.
* `-L` follows symlinked directories but refuses to re-enter one already on the
  current path, which is what `find -L` does.

## Tests

```
make test            # 105 checks against a generated tree
make test-compare    # plus cross-checks against find, grep and rg
make bench           # timings; bench-compare adds the other tools
make bench-compare SCALE=4 RUNS=9   # bigger tree, more samples
```

## License

MIT.
