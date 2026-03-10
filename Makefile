CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -Iinclude
LDFLAGS = -lsystemd -lpthread -lhiredis

TARGET = autoban

# ─── Architecture Detection ───────────────────────────────────────────────────
# Detect host arch; can be overridden: make ARCH=i386
ARCH ?= $(shell uname -m)

# Normalize arch names
ifeq ($(ARCH), x86_64)
    ARCH_FLAGS =
    CROSS_PREFIX =
else ifeq ($(ARCH), i386)
    ARCH_FLAGS = -m32
    CROSS_PREFIX =
    # Need 32-bit libs: apt install gcc-multilib lib32-*
else ifeq ($(ARCH), i686)
    ARCH_FLAGS = -m32
    CROSS_PREFIX =
else ifeq ($(ARCH), aarch64)
    ARCH_FLAGS =
    CROSS_PREFIX = aarch64-linux-gnu-
else ifeq ($(ARCH), armv7l)
    ARCH_FLAGS = -marm
    CROSS_PREFIX = arm-linux-gnueabihf-
endif

ifdef CROSS_PREFIX
    CC = $(CROSS_PREFIX)gcc
endif

CFLAGS += $(ARCH_FLAGS)
LDFLAGS += $(ARCH_FLAGS)

# ─── Ubuntu Version Detection ─────────────────────────────────────────────────
# Ubuntu 18.04 (Bionic): systemd 237, hiredis 0.13
# Ubuntu 20.04 (Focal):  systemd 245, hiredis 0.14
# Ubuntu 22.04 (Jammy):  systemd 249, hiredis 1.0.2
# Ubuntu 24.04 (Noble):  systemd 255, hiredis 1.2.0
UBUNTU_VER := $(shell lsb_release -rs 2>/dev/null || echo "22.04")

# hiredis include path differs on older Ubuntu
HIREDIS_INC := $(shell pkg-config --cflags hiredis 2>/dev/null || echo "-I/usr/include/hiredis")
HIREDIS_LIB := $(shell pkg-config --libs hiredis 2>/dev/null || echo "-lhiredis")
SYSTEMD_INC := $(shell pkg-config --cflags libsystemd 2>/dev/null || echo "")
SYSTEMD_LIB := $(shell pkg-config --libs libsystemd 2>/dev/null || echo "-lsystemd")

CFLAGS  += $(HIREDIS_INC) $(SYSTEMD_INC)
LDFLAGS  = $(ARCH_FLAGS) $(SYSTEMD_LIB) -lpthread $(HIREDIS_LIB)

# ─── Sources ──────────────────────────────────────────────────────────────────
SRC_CORE = $(wildcard src/core/*.c)
SRC_NET  = $(wildcard src/network/*.c)
SRC_SYS  = $(wildcard src/system/*.c)
SRC = $(SRC_CORE) $(SRC_NET) $(SRC_SYS)

OBJS = $(SRC:.c=.o)

# ─── Build Rules ──────────────────────────────────────────────────────────────
all: $(TARGET)
	@echo "[build] arch=$(ARCH) ubuntu=$(UBUNTU_VER) cc=$(CC)"

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/core/%.o: src/core/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/network/%.o: src/network/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/system/%.o: src/system/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install:
	install -m 755 $(TARGET) /usr/local/bin/
	mkdir -p /etc/autoban
	install -m 600 conf/autoban.conf /etc/autoban/autoban.conf
	install -m 644 systemd/autoban.service /etc/systemd/system/
	systemctl daemon-reload
	systemctl enable autoban.service
	systemctl restart autoban.service

test: src/core/ban_hash.o src/core/config.o src/system/ipset.o src/core/queue.o src/core/cache.o src/system/block.o src/network/control_socket.o src/network/redis_sync.o src/system/worker.o src/system/journal.o tests/test_ban_hash.c tests/test_config.c
	$(CC) $(CFLAGS) -o tests/run_test_ban_hash tests/test_ban_hash.c src/core/ban_hash.o $(LDFLAGS)
	$(CC) $(CFLAGS) -o tests/run_test_config tests/test_config.c src/core/config.o $(LDFLAGS)
	./tests/run_test_ban_hash
	./tests/run_test_config

# ─── Code Quality ─────────────────────────────────────────────────────────────
ALL_SRCS := $(shell find src -name '*.c') $(shell find include -name '*.h')

format:
	@which clang-format > /dev/null 2>&1 || (echo "Install: apt install clang-format"; exit 1)
	clang-format -i --style=file $(ALL_SRCS)
	@echo "[format] Done."

tidy:
	@which clang-tidy > /dev/null 2>&1 || (echo "Install: apt install clang-tidy"; exit 1)
	clang-tidy $(SRC) -- $(CFLAGS) 2>&1 | tee /tmp/autoban-tidy.log
	@echo "[tidy] Report saved to /tmp/autoban-tidy.log"

tidy-fix:
	@which clang-tidy > /dev/null 2>&1 || (echo "Install: apt install clang-tidy"; exit 1)
	clang-tidy --fix --fix-errors $(SRC) -- $(CFLAGS)
	@echo "[tidy-fix] Done."

# ─── Cross-compile helpers ────────────────────────────────────────────────────
build-x64:
	$(MAKE) ARCH=x86_64

build-x32:
	$(MAKE) ARCH=i386

build-arm64:
	$(MAKE) ARCH=aarch64

.PHONY: all clean install test format tidy tidy-fix build-x64 build-x32 build-arm64
