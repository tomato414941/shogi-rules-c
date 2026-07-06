CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
CFLAGS += -Iinclude

all: libshogirules.a shogi-rules-cli

src/board.o: src/board.c include/shogi_rules.h
	$(CC) $(CFLAGS) -c $< -o $@

src/cli.o: src/cli.c include/shogi_rules.h
	$(CC) $(CFLAGS) -c $< -o $@

libshogirules.a: src/board.o
	ar rcs $@ $^

shogi-rules-cli: src/cli.o libshogirules.a
	$(CC) $(CFLAGS) -o $@ $^

test: all
	python3 tests/test_oracle.py

clean:
	rm -f src/*.o libshogirules.a shogi-rules-cli

.PHONY: all test clean
