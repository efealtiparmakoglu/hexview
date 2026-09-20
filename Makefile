CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -D_FILE_OFFSET_BITS=64
LDFLAGS ?=
PREFIX  ?= /usr/local
DESTDIR ?=

OBJ = src/main.o src/hexdump.o src/pager.o src/util.o

all: hexview

hexview: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

src/%.o: src/%.c src/util.h src/hexdump.h src/pager.h
	$(CC) $(CFLAGS) -c -o $@ $<

unit: src/tests.o src/hexdump.o src/util.o
	$(CC) $(CFLAGS) -o $@ src/tests.o src/hexdump.o src/util.o

src/tests.o: src/tests.c src/util.h src/hexdump.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: hexview unit
	./unit
	sh tests/run.sh

# Same suite under AddressSanitizer + UBSan.
test-asan:
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Wpedantic -D_FILE_OFFSET_BITS=64 -fsanitize=address,undefined -fno-omit-frame-pointer" test
	$(MAKE) clean
	$(MAKE)

install: hexview
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 hexview "$(DESTDIR)$(PREFIX)/bin/hexview"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/hexview"

clean:
	rm -f hexview unit src/*.o
	rm -rf tests/tmp

.PHONY: all test test-asan install uninstall clean
