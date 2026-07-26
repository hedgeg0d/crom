# crom

The fast file hunter.

## build

```
make
```

## install

```
make install
```

## usage

```
crom '*.c'                    # all .c files in .
crom '*.c' src/               # all .c files in src/
crom --content 'TODO' .       # files containing TODO
crom -n '*.rs' -c 'unsafe'    # rust files with unsafe
crom --bar '*.h'              # with progress bar
crom --no-ignore .            # don't skip .git, etc.
crom -j 4 -c 'search' .       # 4 worker threads
```

## options

```
-n, --name <glob>     match filename pattern
-c, --content <text>  search file contents
-j, --threads <N>     worker threads
--bar                 progress bar + spinner
--no-ignore           don't skip common dirs
-h, --help            show help
-V, --version         print version
```
