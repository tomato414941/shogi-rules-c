/* Line-oriented CLI used by the oracle tests to compare this
 * implementation against shogi-rules-python. */

#include "shogi_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long perft(sr_board *board, int depth) {
    if (depth <= 0) {
        return 1;
    }
    sr_move moves[SR_MAX_MOVES];
    int count = sr_legal_moves(board, moves);
    if (depth == 1) {
        return (unsigned long long)count;
    }
    unsigned long long nodes = 0;
    for (int i = 0; i < count; i++) {
        sr_push(board, moves[i]);
        nodes += perft(board, depth - 1);
        sr_pop(board);
    }
    return nodes;
}

int main(void) {
    sr_init();
    sr_board *board = sr_board_startpos();
    char line[512];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "quit") == 0) {
            break;
        }
        if (strcmp(line, "startpos") == 0) {
            sr_board_set_sfen(board, SR_STARTPOS_SFEN);
            puts("ok");
        } else if (strncmp(line, "sfen ", 5) == 0) {
            puts(sr_board_set_sfen(board, line + 5) ? "ok" : "error");
        } else if (strncmp(line, "push ", 5) == 0) {
            puts(sr_push_usi(board, line + 5) ? "ok" : "error");
        } else if (strcmp(line, "pop") == 0) {
            puts(sr_pop(board) ? "ok" : "error");
        } else if (strcmp(line, "legal") == 0 || strncmp(line, "tactical ", 9) == 0) {
            sr_move moves[SR_MAX_MOVES];
            int count;
            if (line[0] == 'l') {
                count = sr_legal_moves(board, moves);
            } else {
                count = sr_legal_tactical_moves(board, moves, line[9] == '1');
            }
            for (int i = 0; i < count; i++) {
                char usi[8];
                sr_move_usi(moves[i], usi);
                fputs(usi, stdout);
                putchar(i + 1 < count ? ' ' : '\n');
            }
            if (count == 0) {
                putchar('\n');
            }
        } else if (strncmp(line, "perft ", 6) == 0) {
            printf("%llu\n", perft(board, atoi(line + 6)));
        } else if (strcmp(line, "boardsfen") == 0) {
            char sfen[SR_SFEN_MAX];
            puts(sr_board_sfen(board, sfen, sizeof(sfen)) >= 0 ? sfen : "error");
        } else if (strcmp(line, "hash") == 0) {
            printf("%016llx\n", (unsigned long long)sr_board_zobrist(board));
        } else if (strcmp(line, "ischeck") == 0) {
            puts(sr_is_check(board) ? "1" : "0");
        } else if (strcmp(line, "gameover") == 0) {
            puts(sr_is_game_over(board) ? "1" : "0");
        } else if (strcmp(line, "outcome") == 0) {
            static const char *const terminations[] = {"ongoing", "checkmate", "repetition",
                                                       "perpetual_check"};
            sr_outcome outcome = sr_board_outcome(board);
            printf("%s %s\n", terminations[outcome.termination],
                   outcome.winner == SR_BLACK   ? "black"
                   : outcome.winner == SR_WHITE ? "white"
                                                : "-");
        } else if (strcmp(line, "null") == 0) {
            sr_move null_move = {-1, -1, 0, 0};
            puts(sr_push(board, null_move) ? "ok" : "error");
        } else {
            puts("error");
        }
        fflush(stdout);
    }
    sr_board_free(board);
    return 0;
}
