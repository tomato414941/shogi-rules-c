#ifndef SHOGI_RULES_H
#define SHOGI_RULES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_BLACK 0
#define SR_WHITE 1
#define SR_BOARD_SIZE 81

/* Piece type constants. Values intentionally match shogi-rules-python's
 * (and python-shogi's) so that callers mixing implementations stay
 * consistent. */
#define SR_PAWN 1
#define SR_LANCE 2
#define SR_KNIGHT 3
#define SR_SILVER 4
#define SR_GOLD 5
#define SR_BISHOP 6
#define SR_ROOK 7
#define SR_KING 8
#define SR_PROM_PAWN 9
#define SR_PROM_LANCE 10
#define SR_PROM_KNIGHT 11
#define SR_PROM_SILVER 12
#define SR_PROM_BISHOP 13
#define SR_PROM_ROOK 14

#define SR_STARTPOS_SFEN \
    "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1"

/* Upper bound for legal/pseudo-legal move list buffers. */
#define SR_MAX_MOVES 700

/* from_square / to_square: 0..80, or -1 for "none" (drops have
 * from_square == -1; a null move has both == -1).
 * drop_piece_type: piece type for drops, 0 otherwise. */
typedef struct {
    int8_t from_square;
    int8_t to_square;
    uint8_t promotion;
    uint8_t drop_piece_type;
} sr_move;

typedef struct sr_board sr_board;

/* Library initialization: builds the precomputed move/attack/zobrist
 * tables. Called implicitly by board constructors; calling it directly
 * is optional and idempotent. */
void sr_init(void);

/* Board lifecycle. Constructors return NULL on invalid SFEN. */
sr_board *sr_board_startpos(void);
sr_board *sr_board_from_sfen(const char *sfen);
void sr_board_free(sr_board *board);
bool sr_board_set_sfen(sr_board *board, const char *sfen);

/* Accessors. */
int sr_board_turn(const sr_board *board);
int sr_board_move_number(const sr_board *board);
uint64_t sr_board_zobrist(const sr_board *board);
int sr_board_king_square(const sr_board *board, int color);
/* Signed piece at square: >0 black, <0 white, 0 empty. */
int sr_board_piece_at(const sr_board *board, int square);
int sr_board_hand_count(const sr_board *board, int color, int piece_type);
int sr_board_ply(const sr_board *board); /* len(move_stack) */

/* SFEN emission. Returns the number of characters written (excluding the
 * terminating NUL), or -1 if the buffer is too small. */
int sr_board_sfen(const sr_board *board, char *buffer, size_t capacity);

/* Move notation. sr_move_from_usi returns a move with to_square == -1 on
 * parse failure. Buffer for sr_move_usi must hold at least 8 bytes. */
sr_move sr_move_from_usi(const char *usi);
void sr_move_usi(sr_move move, char *buffer);

/* Move generation. Returns the number of moves written to out
 * (at most SR_MAX_MOVES). */
int sr_legal_moves(sr_board *board, sr_move *out);
int sr_legal_tactical_moves(sr_board *board, sr_move *out, bool include_quiet_checks);
bool sr_has_legal_move(sr_board *board);

/* Position transitions. sr_push returns false (and leaves the board
 * unchanged) on structurally invalid moves: capturing a king or dropping
 * a piece that is not in hand. It does not otherwise validate legality,
 * mirroring the Python reference. */
bool sr_push(sr_board *board, sr_move move);
bool sr_push_usi(sr_board *board, const char *usi);
/* Returns false if the move stack is empty. */
bool sr_pop(sr_board *board);

/* State queries. */
bool sr_is_check(sr_board *board);
bool sr_is_game_over(sr_board *board);
bool sr_is_fourfold_repetition(const sr_board *board);
bool sr_is_attacked_by(const sr_board *board, int color, int square);

#ifdef __cplusplus
}
#endif

#endif /* SHOGI_RULES_H */
