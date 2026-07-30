# Changelog

## 0.3.1 — 2026-07-30

### Fixed
* A glob with several `*` and no literal tail backtracked exponentially:
  `a*a*a*a*a*a*a*a*a*b*` against a 64 character name ran for over 30 seconds,
  where find answers in two. The matcher keeps a single backtrack point now.
  Checked against `find -name` over 606 patterns and 400 names.

### Changed
* Built with `gcc -O2` by default instead of `clang -O3`: measurably faster
  here (15% on content search, 17% on the parallel case) on two corpora.
  gcc `-O3` is twice the size and slower than `-O2` on this code. clang still
  builds with `make CC=clang`.

## 0.3.0 — 2026-07-30

First release meant for other people's machines.

### Added
* Several search paths: `crom '*.rs' src tests`.
* `-H/--hidden`, `-u/--unrestricted`, `-L/--follow`, `-E/--exclude`,
  `--max-results`, `-g/--glob`, `-i/--ignore-case`, `--case-sensitive`,
  `--no-messages`.
* Man page, shell completions for bash, zsh and fish, MIT license.

### Changed
* A bare pattern now matches any name containing it, folding case until the
  pattern carries a capital. Patterns with `* ? [` are still whole-name globs.
* Dot files and directories are skipped unless `-H` is given.
* `-i` and smart case apply to `-c` as well.
* Unreadable directories, dropped subtrees and skipped `-e` commands are
  reported instead of silently shortening the results.

### Fixed
* Filesystems that report `DT_UNKNOWN` (FUSE, NFS, overlayfs) yielded no
  matches and no recursion at all.
* `-e` pasted the path into a shell command unquoted, so a file named
  `a; rm -rf x` ran it; an over-long command was truncated and run anyway.
* `--json` escaped only `"` and `\`, so a name containing a newline or tab
  produced invalid JSON.
* Extra positional arguments were silently ignored.

### Performance
* Worker threads are created only once there is work to share, and the queue
  no longer initialises 256 KB per run: an empty-directory search dropped from
  1.03 ms to 0.73 ms.
* The name matcher precomputes everything it used to redo per file.
