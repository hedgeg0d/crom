CC       := clang
CFLAGS   := -nostdlib -ffreestanding -static -O3 -march=native -pipe \
            -flto -fno-stack-protector -fno-pic \
            -fvisibility=hidden -ffunction-sections -fdata-sections \
            -Wall -Wextra -Wno-unused-parameter
ASFLAGS  := -nostdlib -static
LDFLAGS  := -nostdlib -static -Wl,--gc-sections -Wl,--strip-all -fuse-ld=lld
TARGET   := build/crom
SRCDIR   := src
SRCS     := $(wildcard $(SRCDIR)/*.c)
OBJS     := $(patsubst $(SRCDIR)/%.c,build/%.o,$(SRCS)) build/start.o

$(TARGET): $(OBJS)
	@mkdir -p build
	$(CC) $(LDFLAGS) -o $@ $^

build/%.o: $(SRCDIR)/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

build/start.o: $(SRCDIR)/start.S
	@mkdir -p build
	$(CC) $(ASFLAGS) -c -o $@ $<

.PHONY: clean run test

clean:
	rm -rf build

run: $(TARGET)
	$(TARGET)

test: $(TARGET)
	$(TARGET) '*.c' 2>&1 || true
