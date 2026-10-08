CC ?= gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c99 -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -g

COMMON_OBJS = src/frame.o src/headers.o src/protocol.o src/net.o src/util.o
SERVER_OBJS = src/observe.o $(COMMON_OBJS)
CLIENT_OBJS = src/bcurl.o $(COMMON_OBJS)

TEST_OBJS = tests/test_frames tests/test_headers tests/test_protocol

.PHONY: all clean test debug

all: observe bcurl bserve

observe: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

bcurl: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

bserve: observe
	ln -sf observe bserve

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/test_frames: tests/test_frames.o src/frame.o
	$(CC) $(CFLAGS) -o $@ $^

tests/test_headers: tests/test_headers.o src/headers.o
	$(CC) $(CFLAGS) -o $@ $^

tests/test_protocol: tests/test_protocol.o src/protocol.o src/frame.o src/headers.o
	$(CC) $(CFLAGS) -o $@ $^

test: all $(TEST_OBJS)
	@echo "--- Running Unit Tests ---"
	./tests/test_frames
	./tests/test_headers
	./tests/test_protocol
	@echo "--- Running Integration Tests ---"
	chmod +x tests/test_integration.sh
	./tests/test_integration.sh

debug: CFLAGS += -DDEBUG -O0
debug: all

clean:
	rm -f src/*.o tests/*.o observe bcurl bserve $(TEST_OBJS) *.tmp
