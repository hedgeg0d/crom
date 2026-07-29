#!/usr/bin/env python3
"""Builds the file trees used by tests/run.sh and tests/bench.sh.

Usage: gen_fixture.py {correctness|bench} <dir> [scale]

Everything is seeded, so a given (kind, scale) always produces byte-identical
trees. Prints KEY=VALUE metadata on stdout for the shell side to source.
"""
import os
import random
import sys

NEEDLE = b"MAGIC_NEEDLE_7"   # 14 bytes -> Boyer-Moore path (>= 8)
SHORT = b"Mg7"               # 3 bytes  -> SIMD path
CHUNK = 32768                # must match RBUF_SZ in src/pool.c
FILLER = b"lorem ipsum dolor sit amet consectetur\n"


def w(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def correctness(d):
    rnd = random.Random(20240729)
    meta = {}

    # -- plain text, half of them matching -----------------------------------
    hits = 0
    for i in range(200):
        body = FILLER * rnd.randint(1, 30)
        if i % 2 == 0:
            body += NEEDLE + b"\n"
            hits += 1
        w(f"{d}/text/sub{i % 5}/f{i:03d}.txt", body)
    meta["N_TEXT_HIT"] = hits
    meta["N_TEXT_TOTAL"] = 200

    # -- needle straddling the read-chunk boundary at every offset -----------
    n = 0
    for off in range(1, len(NEEDLE)):
        w(f"{d}/chunk/straddle{off:02d}.txt", b"x" * (CHUNK - off) + NEEDLE + b"y" * 64)
        n += 1
    w(f"{d}/chunk/at_edge.txt", b"x" * CHUNK + NEEDLE)
    w(f"{d}/chunk/far.txt", b"x" * (CHUNK * 6 + 17) + NEEDLE + b"z" * 100)
    w(f"{d}/chunk/none.txt", b"x" * (CHUNK * 3))
    meta["N_CHUNK_HIT"] = n + 2
    # same, for the short-needle SIMD path
    w(f"{d}/chunk/short_straddle.txt", b"q" * (CHUNK - 2) + SHORT + b"q" * 10)
    meta["N_SHORT_HIT"] = 1

    # -- binaries: NUL early means "skip unless -a" --------------------------
    for i in range(5):
        w(f"{d}/binary/b{i}.bin", b"\x00\x01\x02" * 40 + NEEDLE + b"\x00" * 100)
    meta["N_BIN_HIT"] = 5
    # NUL only past the 4K probe: crom treats it as text, grep -I does not
    w(f"{d}/lateNUL/late.bin", b"t" * 8192 + NEEDLE + b"\x00" * 16)
    meta["N_LATENUL_HIT"] = 1

    # -- gitignore: root rule must apply to subdirectories -------------------
    w(f"{d}/git/.gitignore", b"*.log\n")
    for p in ("git/a.log", "git/a.txt", "git/sub/b.log", "git/sub/deep/c.log",
              "git/sub/deep/c.txt", "git/other/d.log"):
        w(f"{d}/{p}", NEEDLE + b"\n")
    meta["N_GIT_VISIBLE"] = 2          # a.txt + sub/deep/c.txt
    meta["N_GIT_ALL"] = 6              # with --no-ignore
    # nested negation re-enables .log below git2/sub
    w(f"{d}/git2/.gitignore", b"*.log\n")
    w(f"{d}/git2/sub/.gitignore", b"!*.log\n")
    for p in ("git2/x.log", "git2/sub/y.log", "git2/sub/z.txt"):
        w(f"{d}/{p}", NEEDLE + b"\n")
    meta["N_GIT2_VISIBLE"] = 2         # sub/y.log + sub/z.txt
    # A child .gitignore about *other* patterns must not cancel the parent's
    # rule -- this is the case that actually exercises the parent chain.
    w(f"{d}/git3/.gitignore", b"*.log\n")
    w(f"{d}/git3/sub/.gitignore", b"*.tmp\n")
    for p in ("git3/sub/a.log", "git3/sub/a.tmp", "git3/sub/a.txt"):
        w(f"{d}/{p}", NEEDLE + b"\n")
    meta["N_GIT3_VISIBLE"] = 1         # only a.txt

    # -- names that a newline separator cannot survive -----------------------
    w(f"{d}/weird/dir with space/has space.txt", NEEDLE + b"\n")
    w(f"{d}/weird/tab\tname.txt", NEEDLE + b"\n")
    try:
        w(f"{d}/weird/line\nbreak.txt", NEEDLE + b"\n")
        meta["N_WEIRD"] = 3
    except OSError:
        meta["N_WEIRD"] = 2

    # -- structural odds and ends -------------------------------------------
    os.makedirs(f"{d}/empty/dir", exist_ok=True)
    w(f"{d}/empty/zero.txt", b"")
    deep = d + "/deep" + "/lvl" * 12
    w(f"{deep}/bottom.txt", NEEDLE + b"\n")
    meta["N_DEEP_HIT"] = 1
    os.symlink("../text/sub0/f000.txt", f"{d}/link.txt")
    meta["N_SYMLINK"] = 3   # link.txt + follow/side/to_real + follow/real/loop
    # `build` is on crom's built-in ignore list
    w(f"{d}/build/ignored.txt", NEEDLE + b"\n")

    # -- a directory whose name starts with '-' (needs `--` to reach) --------
    w(f"{d}/-dashdir/inside.txt", NEEDLE + b"\n")
    meta["N_DASHDIR"] = 1

    # -- names for the matcher: substring, smart case, anchoring -------------
    # Deliberately empty so this subtree cannot disturb any content counts.
    for p in ("Makefile", "makefile.old", "notes.PDF", "paper.pdf",
              "sub/MyPdfFile.txt", "file", "myfile.c", "profile"):
        w(f"{d}/names/{p}", b"")
    meta["N_PDF_ANY"] = 3              # notes.PDF paper.pdf sub/MyPdfFile.txt
    meta["N_PDF_STRICT"] = 1           # 'PDF' has a capital: notes.PDF only
    meta["N_FILE_ANY"] = 6             # file Makefile makefile.old myfile.c
    meta["N_FILE_WHOLE"] = 1           #   profile MyPdfFile.txt / just `file`
    meta["N_GLOB_PDF"] = 2             # *.pdf folded: notes.PDF paper.pdf
    meta["N_GLOB_PDF_CS"] = 1          # *.pdf strict: paper.pdf
    meta["N_MAKE_CAP"] = 2             # 'M*' strict: Makefile MyPdfFile.txt
    meta["N_NAMES_TOTAL"] = 8

    # -- hidden entries: skipped unless -H ----------------------------------
    w(f"{d}/hide/visible.txt", NEEDLE + b"\n")
    w(f"{d}/hide/.dotfile.txt", NEEDLE + b"\n")
    w(f"{d}/hide/.dotdir/inside.txt", NEEDLE + b"\n")
    meta["N_HIDE_PLAIN"] = 1           # visible.txt
    meta["N_HIDE_ALL"] = 3             # with -H

    # -- a second root, to check that extra paths are searched ---------------
    w(f"{d}/root2/other.txt", NEEDLE + b"\n")
    meta["N_ROOT2"] = 1

    # -- symlink cycle: -L must terminate ------------------------------------
    w(f"{d}/follow/real/deep.txt", NEEDLE + b"\n")
    os.makedirs(f"{d}/follow/side", exist_ok=True)
    os.symlink("../real", f"{d}/follow/side/to_real")
    os.symlink("..", f"{d}/follow/real/loop")
    meta["N_FOLLOW_PLAIN"] = 1         # follow/real/deep.txt
    meta["N_FOLLOW_L"] = 2             # + side/to_real/deep.txt (loop refused)

    # -- a name that would run a command if -e pasted it into a shell --------
    w(f"{d}/inject/a; touch PWNED", b"")
    w(f"{d}/inject/$(touch SUBST)", b"")
    meta["N_INJECT"] = 2

    # -- enough output to overflow a 64K pipe, for the broken-pipe test ------
    for i in range(3000):
        w(f"{d}/bulk/f{i:04d}.dat", b"")
    meta["N_BULK"] = 3000

    # -- sizes for -s --------------------------------------------------------
    for i, size in enumerate((10, 5000, 200000)):
        w(f"{d}/sizes/s{i}.dat", b"a" * size)
    meta["N_SIZE_BIG"] = 1             # > 100000

    # -- unreadable directory -> exit 2 --------------------------------------
    if os.geteuid() != 0:
        os.makedirs(f"{d}/noperm/inner", exist_ok=True)
        w(f"{d}/noperm/inner/secret.txt", NEEDLE + b"\n")
        os.chmod(f"{d}/noperm", 0o000)
        meta["HAVE_NOPERM"] = 1
    else:
        meta["HAVE_NOPERM"] = 0

    return meta


def bench(d, scale):
    """Text-only, no dotfiles, no .gitignore -- so crom, grep and rg all see
    exactly the same set and the timings stay comparable."""
    rnd = random.Random(4242)
    dirs = 150 * scale
    per_dir = 40
    common = rare = files = total = 0

    for i in range(dirs):
        sub = f"{d}/d{i // 10:03d}/s{i % 10}"
        for j in range(per_dir):
            # skewed sizes: mostly small, a few well past one read chunk
            k = rnd.random()
            if k < 0.80:
                body = FILLER * rnd.randint(1, 40)
            elif k < 0.97:
                body = FILLER * rnd.randint(200, 900)
            else:
                body = FILLER * rnd.randint(2000, 5000)
            if rnd.random() < 0.40:
                body += b"COMMON_TOKEN\n"
                common += 1
            if rnd.random() < 0.002:
                body += b"RARE_TOKEN_XYZ\n"
                rare += 1
            w(f"{sub}/f{j:03d}.txt", body)
            files += 1
            total += len(body)

    return {"B_DIRS": dirs, "B_FILES": files, "B_BYTES": total,
            "B_COMMON": common, "B_RARE": rare}


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    kind, d = sys.argv[1], sys.argv[2]
    scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1

    meta = correctness(d) if kind == "correctness" else bench(d, scale)
    meta["NEEDLE"] = NEEDLE.decode()
    meta["SHORT"] = SHORT.decode()
    meta["CHUNK"] = CHUNK
    for k, v in meta.items():
        print(f"{k}={v}")


if __name__ == "__main__":
    main()
