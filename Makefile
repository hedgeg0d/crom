VERSION  := 0.3.0

CC       := clang
# Tuned for the machine that builds it, which is the point of building from
# source. Override for a portable binary: make ARCH_FLAGS=-march=x86-64-v2
ARCH_FLAGS := -march=native
CFLAGS   := -nostdlib -ffreestanding -static -O3 $(ARCH_FLAGS) -pipe \
            -flto -fno-stack-protector -fno-pic \
            -fvisibility=hidden -ffunction-sections -fdata-sections \
            -DCROM_VERSION=\"$(VERSION)\" \
            -Wall -Wextra -Wno-unused-parameter
ASFLAGS  := -nostdlib -static
LDFLAGS  := -nostdlib -static -Wl,--gc-sections -Wl,--strip-all -fuse-ld=lld
TARGET   := build/crom
PREFIX   := /usr/local
SRCDIR   := src
SRCS     := $(wildcard $(SRCDIR)/*.c)
ASMS     := start clone
OBJS     := $(patsubst $(SRCDIR)/%.c,build/%.o,$(SRCS)) $(patsubst %,build/%.o,$(ASMS))

$(TARGET): $(OBJS)
	@mkdir -p build
	$(CC) $(LDFLAGS) -o $@ $^

build/%.o: $(SRCDIR)/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: $(SRCDIR)/%.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c -o $@ $<

.PHONY: clean run test test-compare bench bench-compare install uninstall

clean:
	rm -rf build

run: $(TARGET)
	$(TARGET) .

test: $(TARGET)
	@bash tests/run.sh

test-compare: $(TARGET)
	@bash tests/run.sh --compare

bench: $(TARGET)
	@bash tests/bench.sh

bench-compare: $(TARGET)
	@bash tests/bench.sh --compare

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/crom

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/crom
