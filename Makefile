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

tests/test_board: tests/test_board.c include/shogi_rules.h libshogirules.a
	$(CC) $(CFLAGS) -o $@ $< libshogirules.a

test: all tests/test_board
	tests/test_board
	python3 tests/test_oracle.py

clean:
	rm -f src/*.o libshogirules.a shogi-rules-cli tests/test_board

.PHONY: all test clean
