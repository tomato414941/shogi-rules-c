/* Tests of the C API that the oracle tests cannot reach through the CLI. */

#include "shogi_rules.h"

#include <stdio.h>
#include <string.h>

/* The longest SFEN of a position made of the standard forty pieces: every
 * promotable piece promoted on the board, each alone between empty squares,
 * the golds in hand, and a ten-digit move number. */
#define LONGEST_SFEN                                                       \
    "1+p1+p1+p1+p1/1+p1+p1+p1+p1/1+p1+p1+p1+p1/1+p1+p1+p1+p1/1K1k1+p1+p1/" \
    "1+l1+l1+l1+l1/1+n1+n1+n1+n1/1+s1+s1+s1+s1/1+b1+b1+r1+r1 b 2G2g "      \
    "1000000000"

static int failures;

static void expect(bool condition, const char *test, const char *what) {
    if (!condition) {
        printf("FAIL %s: %s\n", test, what);
        failures++;
    }
}

static void test_writes_the_longest_sfen_within_sfen_max(void) {
    const char *test = __func__;
    sr_board *board = sr_board_from_sfen(LONGEST_SFEN);
    expect(board != NULL, test, "the position is read");
    if (board == NULL) {
        return;
    }
    char sfen[SR_SFEN_MAX];
    expect(sr_board_sfen(board, sfen, sizeof(sfen)) == (int)strlen(LONGEST_SFEN), test,
           "returns its length");
    expect(strcmp(sfen, LONGEST_SFEN) == 0, test, "writes it");
    sr_board_free(board);
}

static void test_leaves_a_short_buffer_empty(void) {
    const char *test = __func__;
    sr_board *board = sr_board_from_sfen(LONGEST_SFEN);
    expect(board != NULL, test, "the position is read");
    if (board == NULL) {
        return;
    }
    char sfen[SR_SFEN_MAX];
    memset(sfen, 'x', sizeof(sfen));
    size_t capacity = strlen(LONGEST_SFEN); /* no room for the terminating NUL */
    expect(sr_board_sfen(board, sfen, capacity) == -1, test, "returns -1");
    expect(sfen[0] == '\0', test, "leaves an empty string");
    expect(sfen[capacity] == 'x', test, "writes nothing past capacity");
    sr_board_free(board);
}

int main(void) {
    test_writes_the_longest_sfen_within_sfen_max();
    test_leaves_a_short_buffer_empty();
    if (failures > 0) {
        return 1;
    }
    puts("test_board: ok");
    return 0;
}
