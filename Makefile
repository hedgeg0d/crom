CC       := clang
CFLAGS   := -nostdlib -ffreestanding -static -O3 -march=native -pipe \
            -flto -fno-stack-protector -fno-pic \
            -fvisibility=hidden -ffunction-sections -fdata-sections \
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

.PHONY: clean run test install uninstall

clean:
	rm -rf build

run: $(TARGET)
	$(TARGET) .

test: $(TARGET)
	$(TARGET) '*.c' 2>&1 || true

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/crom

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/crom
