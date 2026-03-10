CC = gcc
CFLAGS = -Wall -g -O2 -Iinclude
LDFLAGS = -lsystemd -lpthread -lhiredis

TARGET = autoban

# Find all .c files in subdirectories
SRC_CORE = $(wildcard src/core/*.c)
SRC_NET  = $(wildcard src/network/*.c)
SRC_SYS  = $(wildcard src/system/*.c)
SRC = $(SRC_CORE) $(SRC_NET) $(SRC_SYS)

# Output objects
OBJS = $(SRC:.c=.o)

all: $(TARGET)

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
