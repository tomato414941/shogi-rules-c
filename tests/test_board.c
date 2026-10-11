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

static void test_refuses_pushes_that_break_the_board(void) {
    const char *test = __func__;
    static const struct {
        const char *sfen;
        sr_move move;
    } cases[] = {
        {SR_STARTPOS_SFEN, {-1, -1, 1, 0}},
        {SR_STARTPOS_SFEN, {-1, -1, 0, SR_PAWN}},
        {SR_STARTPOS_SFEN, {76, -1, 0, 0}},
        {SR_STARTPOS_SFEN, {76, 81, 0, 0}},
        {SR_STARTPOS_SFEN, {81, 40, 0, 0}},
        {SR_STARTPOS_SFEN, {-2, 40, 0, 0}},
        {SR_STARTPOS_SFEN, {60, -5, 0, 0}},
        {SR_STARTPOS_SFEN, {-1, 100, 0, SR_PAWN}},
        {SR_STARTPOS_SFEN, {40, 31, 0, 0}},
        {SR_STARTPOS_SFEN, {4, 13, 0, 0}},
        {SR_STARTPOS_SFEN, {75, 67, 1, 0}},
        {SR_STARTPOS_SFEN, {76, 67, 1, 0}},
        {SR_STARTPOS_SFEN, {75, 74, 0, 0}},
        {SR_STARTPOS_SFEN, {75, 76, 0, 0}},
        {SR_STARTPOS_SFEN, {-1, 40, 0, SR_PAWN}},
        {SR_STARTPOS_SFEN, {-1, 40, 0, SR_KING}},
        {SR_STARTPOS_SFEN, {-1, 40, 0, SR_PROM_PAWN}},
        {SR_STARTPOS_SFEN, {-1, 40, 0, 200}},
        {"4k4/9/9/9/9/9/4P4/9/4K4 b P 1", {-1, 58, 0, SR_PAWN}},
        {"4k4/9/9/9/9/9/4P4/9/4K4 b P 1", {-1, 40, 1, SR_PAWN}},
        {"4k4/9/9/9/9/9/4P4/9/4K4 b P 1", {58, 49, 0, SR_PAWN}},
        {"4k4/9/9/9/9/9/4G4/9/4K4 b - 1", {58, 49, 1, 0}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char what[64];
        sr_board *board = sr_board_from_sfen(cases[i].sfen);
        snprintf(what, sizeof(what), "case %zu: the position is read", i);
        expect(board != NULL, test, what);
        if (board == NULL) {
            continue;
        }
        char before[SR_SFEN_MAX];
        char after[SR_SFEN_MAX];
        sr_board_sfen(board, before, sizeof(before));
        uint64_t hash = sr_board_zobrist(board);
        snprintf(what, sizeof(what), "case %zu: refuses the move", i);
        expect(!sr_push(board, cases[i].move), test, what);
        sr_board_sfen(board, after, sizeof(after));
        snprintf(what, sizeof(what), "case %zu: leaves the board unchanged", i);
        expect(strcmp(before, after) == 0 && sr_board_zobrist(board) == hash &&
                   sr_board_ply(board) == 0,
               test, what);
        sr_board_free(board);
    }
}

static void test_plays_and_takes_back_a_null_move(void) {
    const char *test = __func__;
    sr_board *board = sr_board_startpos();
    sr_move null_move = {-1, -1, 0, 0};

    expect(sr_push(board, null_move), test, "plays it");
    expect(sr_board_turn(board) == SR_WHITE && sr_board_move_number(board) == 2, test,
           "passes the turn");
    expect(sr_pop(board) && sr_board_turn(board) == SR_BLACK, test, "takes it back");
    sr_board_free(board);
}

int main(void) {
    test_writes_the_longest_sfen_within_sfen_max();
    test_leaves_a_short_buffer_empty();
    test_refuses_pushes_that_break_the_board();
    test_plays_and_takes_back_a_null_move();
    if (failures > 0) {
        return 1;
    }
    puts("test_board: ok");
    return 0;
}
