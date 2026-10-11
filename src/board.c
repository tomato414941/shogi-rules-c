/* Port of shogi-rules-python (our own MIT reference implementation).
 * Precomputed tables and the zobrist scheme match the reference exactly,
 * so hashes are comparable across the two implementations. */

#include "shogi_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_PIECE_TYPES 15 /* 0 unused, 1..14 */
#define MAX_UNDO 2048
/* With at most MAX_UNDO moves pushed on top, the move number stays an int
 * of ten digits. */
#define MAX_MOVE_NUMBER 1000000000

static const int DROP_PIECES[] = {SR_ROOK, SR_BISHOP, SR_GOLD, SR_SILVER,
                                  SR_KNIGHT, SR_LANCE, SR_PAWN};
#define NUM_DROP_PIECES 7

static const int STEP_ATTACK_PIECES[] = {
    SR_PAWN, SR_KNIGHT, SR_SILVER, SR_GOLD, SR_KING, SR_PROM_PAWN,
    SR_PROM_LANCE, SR_PROM_KNIGHT, SR_PROM_SILVER, SR_PROM_BISHOP,
    SR_PROM_ROOK,
};
#define NUM_STEP_ATTACK_PIECES 11

/* The pieces of a shogi set besides the kings, by unpromoted type; promoted
 * pieces count as their unpromoted kind. */
static const int PIECE_SET[NUM_PIECE_TYPES] = {
    0, 18, 4, 4, 4, 4, 2, 2, /* P L N S G B R */
};

static const char PIECE_CHARS[NUM_PIECE_TYPES] = {
    /* index by unpromoted piece type; 0 unused */
    '\0', 'P', 'L', 'N', 'S', 'G', 'B', 'R', 'K',
    '\0', '\0', '\0', '\0', '\0', '\0',
};

static int promoted_of(int piece_type) {
    switch (piece_type) {
    case SR_PAWN: return SR_PROM_PAWN;
    case SR_LANCE: return SR_PROM_LANCE;
    case SR_KNIGHT: return SR_PROM_KNIGHT;
    case SR_SILVER: return SR_PROM_SILVER;
    case SR_BISHOP: return SR_PROM_BISHOP;
    case SR_ROOK: return SR_PROM_ROOK;
    default: return 0;
    }
}

static int unpromoted_of(int piece_type) {
    switch (piece_type) {
    case SR_PROM_PAWN: return SR_PAWN;
    case SR_PROM_LANCE: return SR_LANCE;
    case SR_PROM_KNIGHT: return SR_KNIGHT;
    case SR_PROM_SILVER: return SR_SILVER;
    case SR_PROM_BISHOP: return SR_BISHOP;
    case SR_PROM_ROOK: return SR_ROOK;
    default: return piece_type;
    }
}

static int piece_from_char(char upper) {
    switch (upper) {
    case 'P': return SR_PAWN;
    case 'L': return SR_LANCE;
    case 'N': return SR_KNIGHT;
    case 'S': return SR_SILVER;
    case 'G': return SR_GOLD;
    case 'B': return SR_BISHOP;
    case 'R': return SR_ROOK;
    case 'K': return SR_KING;
    default: return 0;
    }
}

static int signed_piece(int color, int piece_type) {
    return color == SR_BLACK ? piece_type : -piece_type;
}

static int piece_color(int signed_piece_value) {
    return signed_piece_value > 0 ? SR_BLACK : SR_WHITE;
}

static int forward_of(int color) { return color == SR_BLACK ? -1 : 1; }

static int offset_square(int square, int df, int dr) {
    int file_index = square % 9 + df;
    int rank = square / 9 + dr;
    if (file_index < 0 || file_index >= 9 || rank < 0 || rank >= 9) {
        return -1;
    }
    return rank * 9 + file_index;
}

/* Direction tables, mirroring _step_directions / _slide_directions. */
typedef struct {
    int8_t df;
    int8_t dr;
} sr_dir;

static int step_directions(int piece_type, int color, sr_dir out[8]) {
    int forward = forward_of(color);
    int backward = -forward;
    switch (piece_type) {
    case SR_PAWN:
        out[0] = (sr_dir){0, (int8_t)forward};
        return 1;
    case SR_KNIGHT:
        out[0] = (sr_dir){-1, (int8_t)(2 * forward)};
        out[1] = (sr_dir){1, (int8_t)(2 * forward)};
        return 2;
    case SR_SILVER:
        out[0] = (sr_dir){-1, (int8_t)forward};
        out[1] = (sr_dir){0, (int8_t)forward};
        out[2] = (sr_dir){1, (int8_t)forward};
        out[3] = (sr_dir){-1, (int8_t)backward};
        out[4] = (sr_dir){1, (int8_t)backward};
        return 5;
    case SR_GOLD:
    case SR_PROM_PAWN:
    case SR_PROM_LANCE:
    case SR_PROM_KNIGHT:
    case SR_PROM_SILVER:
        out[0] = (sr_dir){-1, (int8_t)forward};
        out[1] = (sr_dir){0, (int8_t)forward};
        out[2] = (sr_dir){1, (int8_t)forward};
        out[3] = (sr_dir){-1, 0};
        out[4] = (sr_dir){1, 0};
        out[5] = (sr_dir){0, (int8_t)backward};
        return 6;
    case SR_KING:
        out[0] = (sr_dir){-1, -1};
        out[1] = (sr_dir){0, -1};
        out[2] = (sr_dir){1, -1};
        out[3] = (sr_dir){-1, 0};
        out[4] = (sr_dir){1, 0};
        out[5] = (sr_dir){-1, 1};
        out[6] = (sr_dir){0, 1};
        out[7] = (sr_dir){1, 1};
        return 8;
    case SR_PROM_BISHOP:
        out[0] = (sr_dir){0, -1};
        out[1] = (sr_dir){-1, 0};
        out[2] = (sr_dir){1, 0};
        out[3] = (sr_dir){0, 1};
        return 4;
    case SR_PROM_ROOK:
        out[0] = (sr_dir){-1, -1};
        out[1] = (sr_dir){1, -1};
        out[2] = (sr_dir){-1, 1};
        out[3] = (sr_dir){1, 1};
        return 4;
    default:
        return 0;
    }
}

static int slide_directions(int piece_type, int color, sr_dir out[4]) {
    switch (piece_type) {
    case SR_LANCE:
        out[0] = (sr_dir){0, (int8_t)forward_of(color)};
        return 1;
    case SR_BISHOP:
    case SR_PROM_BISHOP:
        out[0] = (sr_dir){-1, -1};
        out[1] = (sr_dir){1, -1};
        out[2] = (sr_dir){-1, 1};
        out[3] = (sr_dir){1, 1};
        return 4;
    case SR_ROOK:
    case SR_PROM_ROOK:
        out[0] = (sr_dir){0, -1};
        out[1] = (sr_dir){-1, 0};
        out[2] = (sr_dir){1, 0};
        out[3] = (sr_dir){0, 1};
        return 4;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Precomputed tables                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t length;
    uint8_t squares[8];
} sr_ray;

typedef struct {
    uint8_t ray_count;
    sr_ray rays[8];
} sr_square_rays;

/* MOVE_RAYS[color][piece_type][square] */
static sr_square_rays MOVE_RAYS[2][NUM_PIECE_TYPES][SR_BOARD_SIZE];

typedef struct {
    uint8_t from_square;
    int8_t piece; /* signed piece */
} sr_step_source;

typedef struct {
    uint8_t count;
    sr_step_source entries[54];
} sr_step_sources;

/* STEP_ATTACK_SOURCES[color][target_square] */
static sr_step_sources STEP_ATTACK_SOURCES[2][SR_BOARD_SIZE];

typedef struct {
    sr_ray ray;
    int8_t pieces[2]; /* signed pieces; 0-terminated when only one */
    uint8_t piece_count;
} sr_ray_source;

typedef struct {
    uint8_t count;
    sr_ray_source entries[9];
} sr_ray_sources;

/* RAY_ATTACK_SOURCES[color][target_square] */
static sr_ray_sources RAY_ATTACK_SOURCES[2][SR_BOARD_SIZE];

/* ZOBRIST_BOARD[signed_piece + 14][square] */
static uint64_t ZOBRIST_BOARD[29][SR_BOARD_SIZE];
/* ZOBRIST_HAND[color][piece_type][count], piece_type 1..7, count 0..31 */
static uint64_t ZOBRIST_HAND[2][8][32];
static uint64_t ZOBRIST_TURN;

static uint64_t splitmix64(uint64_t value) {
    value += UINT64_C(0x9E3779B97F4A7C15);
    value = (value ^ (value >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94D049BB133111EB);
    return value ^ (value >> 31);
}

static void build_ray(int square, int df, int dr, sr_ray *ray) {
    ray->length = 0;
    int current = offset_square(square, df, dr);
    while (current >= 0) {
        ray->squares[ray->length++] = (uint8_t)current;
        current = offset_square(current, df, dr);
    }
}

static bool tables_ready = false;

void sr_init(void) {
    if (tables_ready) {
        return;
    }

    /* MOVE_RAYS: steps become single-square rays, slides full rays. */
    for (int color = 0; color < 2; color++) {
        for (int piece_type = 1; piece_type < NUM_PIECE_TYPES; piece_type++) {
            for (int square = 0; square < SR_BOARD_SIZE; square++) {
                sr_square_rays *entry = &MOVE_RAYS[color][piece_type][square];
                entry->ray_count = 0;
                sr_dir dirs[8];
                int step_count = step_directions(piece_type, color, dirs);
                for (int i = 0; i < step_count; i++) {
                    int target = offset_square(square, dirs[i].df, dirs[i].dr);
                    if (target >= 0) {
                        sr_ray *ray = &entry->rays[entry->ray_count++];
                        ray->length = 1;
                        ray->squares[0] = (uint8_t)target;
                    }
                }
                int slide_count = slide_directions(piece_type, color, dirs);
                for (int i = 0; i < slide_count; i++) {
                    sr_ray *ray = &entry->rays[entry->ray_count++];
                    build_ray(square, dirs[i].df, dirs[i].dr, ray);
                }
            }
        }
    }

    /* STEP_ATTACK_SOURCES: reverse step directions. */
    for (int color = 0; color < 2; color++) {
        for (int target = 0; target < SR_BOARD_SIZE; target++) {
            sr_step_sources *sources = &STEP_ATTACK_SOURCES[color][target];
            sources->count = 0;
            for (int i = 0; i < NUM_STEP_ATTACK_PIECES; i++) {
                int piece_type = STEP_ATTACK_PIECES[i];
                sr_dir dirs[8];
                int count = step_directions(piece_type, color, dirs);
                for (int d = 0; d < count; d++) {
                    int from = offset_square(target, -dirs[d].df, -dirs[d].dr);
                    if (from >= 0) {
                        sr_step_source *entry = &sources->entries[sources->count++];
                        entry->from_square = (uint8_t)from;
                        entry->piece = (int8_t)signed_piece(color, piece_type);
                    }
                }
            }
        }
    }

    /* RAY_ATTACK_SOURCES: 8 slider rays plus the color-specific lance ray. */
    for (int color = 0; color < 2; color++) {
        static const struct {
            int8_t df;
            int8_t dr;
            uint8_t bishop_like; /* 1: bishop/prom_bishop, 0: rook/prom_rook */
        } common[8] = {
            {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1},
            {0, -1, 0},  {-1, 0, 0}, {1, 0, 0},  {0, 1, 0},
        };
        for (int target = 0; target < SR_BOARD_SIZE; target++) {
            sr_ray_sources *sources = &RAY_ATTACK_SOURCES[color][target];
            sources->count = 0;
            for (int i = 0; i < 8; i++) {
                sr_ray_source *entry = &sources->entries[sources->count++];
                build_ray(target, common[i].df, common[i].dr, &entry->ray);
                if (common[i].bishop_like) {
                    entry->pieces[0] = (int8_t)signed_piece(color, SR_BISHOP);
                    entry->pieces[1] = (int8_t)signed_piece(color, SR_PROM_BISHOP);
                } else {
                    entry->pieces[0] = (int8_t)signed_piece(color, SR_ROOK);
                    entry->pieces[1] = (int8_t)signed_piece(color, SR_PROM_ROOK);
                }
                entry->piece_count = 2;
            }
            sr_ray_source *lance = &sources->entries[sources->count++];
            build_ray(target, 0, -forward_of(color), &lance->ray);
            lance->pieces[0] = (int8_t)signed_piece(color, SR_LANCE);
            lance->pieces[1] = 0;
            lance->piece_count = 1;
        }
    }

    /* Zobrist tables, matching the reference seeds exactly. */
    for (int piece_index = 0; piece_index < 29; piece_index++) {
        for (int square = 0; square < SR_BOARD_SIZE; square++) {
            ZOBRIST_BOARD[piece_index][square] =
                splitmix64((uint64_t)(10000 + piece_index * SR_BOARD_SIZE + square));
        }
    }
    for (int color = 0; color < 2; color++) {
        for (int i = 0; i < NUM_DROP_PIECES; i++) {
            int piece_type = DROP_PIECES[i];
            for (int count = 0; count < 32; count++) {
                ZOBRIST_HAND[color][piece_type][count] = splitmix64(
                    (uint64_t)(100000 + color * 10000 + piece_type * 100 + count));
            }
        }
    }
    ZOBRIST_TURN = splitmix64(999999);
    tables_ready = true;
}

/* ------------------------------------------------------------------ */
/* Board                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    sr_move move;
    int8_t moved_piece;
    int8_t captured_piece;
    uint8_t previous_turn;
    int previous_move_number;
    uint64_t previous_hash;
} sr_undo;

struct sr_board {
    int8_t board[SR_BOARD_SIZE];
    int turn;
    int move_number;
    uint8_t hands[2][NUM_PIECE_TYPES];
    int king_squares[2];
    uint64_t hash;
    sr_undo undo_stack[MAX_UNDO];
    int undo_count;
};

static uint64_t board_hash(int signed_piece_value, int square) {
    return ZOBRIST_BOARD[signed_piece_value + 14][square];
}

static uint64_t hand_hash(int color, int piece_type, int count) {
    if (count <= 0) {
        return 0;
    }
    if (count >= 32) {
        return splitmix64(
            (uint64_t)(200000 + color * 10000 + piece_type * 100 + count));
    }
    return ZOBRIST_HAND[color][piece_type][count];
}

static uint64_t compute_hash(const sr_board *board) {
    uint64_t value = board->turn == SR_WHITE ? ZOBRIST_TURN : 0;
    for (int square = 0; square < SR_BOARD_SIZE; square++) {
        int piece = board->board[square];
        if (piece) {
            value ^= board_hash(piece, square);
        }
    }
    for (int color = 0; color < 2; color++) {
        for (int i = 0; i < NUM_DROP_PIECES; i++) {
            int piece_type = DROP_PIECES[i];
            int count = board->hands[color][piece_type];
            if (count > 0) {
                value ^= hand_hash(color, piece_type, count);
            }
        }
    }
    return value;
}

static bool find_king_squares(sr_board *board) {
    int kings[2] = {0, 0};
    for (int square = 0; square < SR_BOARD_SIZE; square++) {
        int piece = board->board[square];
        if (abs(piece) == SR_KING) {
            board->king_squares[piece_color(piece)] = square;
            kings[piece_color(piece)]++;
        }
    }
    return kings[SR_BLACK] == 1 && kings[SR_WHITE] == 1;
}

static bool can_move_without_promotion(int piece_type, int to_square, int color);

/* A position that can occur in a game: no more pieces than a set has, no two
 * unpromoted pawns of a side on a file, no piece where it can never move,
 * and the side not to move not in check. */
static bool is_possible_position(const sr_board *board) {
    int counts[NUM_PIECE_TYPES] = {0};
    bool pawn_files[2][9] = {{false}};
    for (int square = 0; square < SR_BOARD_SIZE; square++) {
        int piece = board->board[square];
        int piece_type = abs(piece);
        if (piece_type == 0 || piece_type == SR_KING) {
            continue;
        }
        int color = piece_color(piece);
        counts[unpromoted_of(piece_type)]++;
        if (!can_move_without_promotion(piece_type, square, color)) {
            return false;
        }
        if (piece_type == SR_PAWN) {
            if (pawn_files[color][square % 9]) {
                return false;
            }
            pawn_files[color][square % 9] = true;
        }
    }
    for (int i = 0; i < NUM_DROP_PIECES; i++) {
        int piece_type = DROP_PIECES[i];
        int count = counts[piece_type] + board->hands[SR_BLACK][piece_type] +
                    board->hands[SR_WHITE][piece_type];
        if (count > PIECE_SET[piece_type]) {
            return false;
        }
    }
    return !sr_is_attacked_by(board, board->turn, board->king_squares[1 - board->turn]);
}

/* SFEN fields are separated by runs of ASCII whitespace, whatever the
 * locale. */
static bool is_sfen_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

typedef struct {
    const char *text;
    size_t length;
} sfen_field;

/* Returns the number of fields, or max + 1 when there are more than max. */
static int split_sfen(const char *sfen, sfen_field *fields, int max) {
    int count = 0;
    const char *cursor = sfen;
    for (;;) {
        while (is_sfen_space(*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            return count;
        }
        if (count == max) {
            return max + 1;
        }
        fields[count].text = cursor;
        while (*cursor != '\0' && !is_sfen_space(*cursor)) {
            cursor++;
        }
        fields[count].length = (size_t)(cursor - fields[count].text);
        count++;
    }
}

/* Upper case for black, lower case for white; 0 for any other character. */
static int piece_from_letter(char c, int *color) {
    if (c >= 'a' && c <= 'z') {
        *color = SR_WHITE;
        return piece_from_char((char)(c - 'a' + 'A'));
    }
    *color = SR_BLACK;
    return piece_from_char(c);
}

/* Nine ranks from a to i separated by '/', each nine squares from file 9 to
 * 1: a digit for that many empty squares, a letter for a piece, '+' before a
 * letter for a promoted one. */
static bool parse_board_field(sr_board *board, sfen_field field) {
    int rank = 0;
    int file_index = 0;
    for (size_t i = 0; i < field.length; i++) {
        char c = field.text[i];
        if (c == '/') {
            if (file_index != 9 || rank == 8) {
                return false;
            }
            rank++;
            file_index = 0;
            continue;
        }
        if (c >= '1' && c <= '9') {
            file_index += c - '0';
            if (file_index > 9) {
                return false;
            }
            continue;
        }
        bool promoted = c == '+';
        if (promoted) {
            if (++i == field.length) {
                return false;
            }
            c = field.text[i];
        }
        int color;
        int piece_type = piece_from_letter(c, &color);
        if (piece_type == 0 || file_index == 9) {
            return false;
        }
        if (promoted) {
            piece_type = promoted_of(piece_type);
            if (piece_type == 0) {
                return false;
            }
        }
        board->board[rank * 9 + file_index] = (int8_t)signed_piece(color, piece_type);
        file_index++;
    }
    return rank == 8 && file_index == 9;
}

/* '-' for none, or each kind a side holds once, its count before it when
 * more than one. No kind has more pieces than the pawns, so a larger count
 * is refused before it can overflow. */
static bool parse_hand_field(sr_board *board, sfen_field field) {
    if (field.length == 1 && field.text[0] == '-') {
        return true;
    }
    size_t i = 0;
    while (i < field.length) {
        int count = 0;
        size_t digits = 0;
        for (; i < field.length && field.text[i] >= '0' && field.text[i] <= '9'; i++) {
            count = count * 10 + (field.text[i] - '0');
            if (count > PIECE_SET[SR_PAWN]) {
                return false;
            }
            digits++;
        }
        if (i == field.length) {
            return false;
        }
        int color;
        int piece_type = piece_from_letter(field.text[i++], &color);
        if (piece_type == 0 || piece_type == SR_KING || (digits > 0 && count == 0) ||
            board->hands[color][piece_type] != 0) {
            return false;
        }
        board->hands[color][piece_type] = (uint8_t)(digits > 0 ? count : 1);
    }
    return true;
}

static bool parse_move_number_field(sr_board *board, sfen_field field) {
    long long value = 0;
    for (size_t i = 0; i < field.length; i++) {
        char c = field.text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
        if (value > MAX_MOVE_NUMBER) {
            return false;
        }
    }
    if (value < 1) {
        return false;
    }
    board->move_number = (int)value;
    return true;
}

static bool parse_sfen_into(sr_board *board, const char *sfen) {
    memset(board->board, 0, sizeof(board->board));
    memset(board->hands, 0, sizeof(board->hands));
    board->undo_count = 0;
    board->move_number = 1;

    sfen_field fields[4];
    int count = split_sfen(sfen, fields, 4);
    if (count < 3 || count > 4 || !parse_board_field(board, fields[0])) {
        return false;
    }
    if (fields[1].length != 1 || (fields[1].text[0] != 'b' && fields[1].text[0] != 'w')) {
        return false;
    }
    board->turn = fields[1].text[0] == 'b' ? SR_BLACK : SR_WHITE;
    if (!parse_hand_field(board, fields[2]) ||
        (count == 4 && !parse_move_number_field(board, fields[3]))) {
        return false;
    }
    if (!find_king_squares(board) || !is_possible_position(board)) {
        return false;
    }
    board->hash = compute_hash(board);
    return true;
}

sr_board *sr_board_from_sfen(const char *sfen) {
    sr_init();
    sr_board *board = calloc(1, sizeof(sr_board));
    if (board == NULL) {
        return NULL;
    }
    if (!parse_sfen_into(board, sfen)) {
        free(board);
        return NULL;
    }
    return board;
}

sr_board *sr_board_startpos(void) { return sr_board_from_sfen(SR_STARTPOS_SFEN); }

bool sr_board_set_sfen(sr_board *board, const char *sfen) {
    sr_board copy;
    if (!parse_sfen_into(&copy, sfen)) {
        return false;
    }
    *board = copy;
    return true;
}

void sr_board_free(sr_board *board) { free(board); }

int sr_board_turn(const sr_board *board) { return board->turn; }
int sr_board_move_number(const sr_board *board) { return board->move_number; }
uint64_t sr_board_zobrist(const sr_board *board) { return board->hash; }
int sr_board_king_square(const sr_board *board, int color) {
    return board->king_squares[color];
}
int sr_board_piece_at(const sr_board *board, int square) {
    return board->board[square];
}
int sr_board_hand_count(const sr_board *board, int color, int piece_type) {
    return board->hands[color][piece_type];
}
int sr_board_ply(const sr_board *board) { return board->undo_count; }

/* Appends to the caller's buffer, never past its capacity, keeping room for
 * the terminating NUL; overflowed records that something did not fit. */
typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflowed;
} sfen_writer;

static void put_char(sfen_writer *writer, char c) {
    if (writer->length + 1 >= writer->capacity) {
        writer->overflowed = true;
        return;
    }
    writer->buffer[writer->length++] = c;
}

static void put_number(sfen_writer *writer, int value) {
    char digits[16];
    int count = snprintf(digits, sizeof(digits), "%d", value);
    for (int i = 0; i < count; i++) {
        put_char(writer, digits[i]);
    }
}

int sr_board_sfen(const sr_board *board, char *buffer, size_t capacity) {
    sfen_writer writer = {buffer, capacity, 0, false};
    for (int rank = 0; rank < 9; rank++) {
        int empty = 0;
        for (int file_index = 0; file_index < 9; file_index++) {
            int piece = board->board[rank * 9 + file_index];
            if (piece == 0) {
                empty++;
                continue;
            }
            if (empty) {
                put_char(&writer, (char)('0' + empty));
                empty = 0;
            }
            int piece_type = abs(piece);
            int base = unpromoted_of(piece_type);
            if (piece_type != base) {
                put_char(&writer, '+');
            }
            char symbol = PIECE_CHARS[base];
            put_char(&writer, piece > 0 ? symbol : (char)tolower((unsigned char)symbol));
        }
        if (empty) {
            put_char(&writer, (char)('0' + empty));
        }
        if (rank < 8) {
            put_char(&writer, '/');
        }
    }
    put_char(&writer, ' ');
    put_char(&writer, board->turn == SR_BLACK ? 'b' : 'w');
    put_char(&writer, ' ');

    int hand_length = 0;
    for (int color = 0; color < 2; color++) {
        for (int i = 0; i < NUM_DROP_PIECES; i++) {
            int piece_type = DROP_PIECES[i];
            int count = board->hands[color][piece_type];
            if (count <= 0) {
                continue;
            }
            if (count > 1) {
                put_number(&writer, count);
            }
            char symbol = PIECE_CHARS[piece_type];
            put_char(&writer,
                     color == SR_BLACK ? symbol : (char)tolower((unsigned char)symbol));
            hand_length++;
        }
    }
    if (hand_length == 0) {
        put_char(&writer, '-');
    }
    put_char(&writer, ' ');
    put_number(&writer, board->move_number);

    if (writer.overflowed) {
        if (capacity > 0) {
            buffer[0] = '\0';
        }
        return -1;
    }
    buffer[writer.length] = '\0';
    return (int)writer.length;
}

/* ------------------------------------------------------------------ */
/* Move notation                                                       */
/* ------------------------------------------------------------------ */

static int square_from_name(const char *name) {
    if (name[0] < '1' || name[0] > '9' || name[1] < 'a' || name[1] > 'i') {
        return -1;
    }
    int file_index = 9 - (name[0] - '0');
    int rank = name[1] - 'a';
    return rank * 9 + file_index;
}

static void square_name(int square, char out[3]) {
    out[0] = (char)('0' + (9 - square % 9));
    out[1] = (char)('a' + square / 9);
    out[2] = '\0';
}

sr_move sr_move_from_usi(const char *usi) {
    sr_move move = {-1, -1, 0, 0};
    size_t length = strlen(usi);
    if (length == 4 && usi[1] == '*') {
        int piece_type = piece_from_char(usi[0]);
        int to_square = square_from_name(usi + 2);
        if (piece_type == 0 || piece_type == SR_KING || to_square < 0) {
            return move;
        }
        move.to_square = (int8_t)to_square;
        move.drop_piece_type = (uint8_t)piece_type;
        return move;
    }
    if (length != 4 && (length != 5 || usi[4] != '+')) {
        return move;
    }
    int from_square = square_from_name(usi);
    int to_square = square_from_name(usi + 2);
    if (from_square < 0 || to_square < 0) {
        return move;
    }
    move.from_square = (int8_t)from_square;
    move.to_square = (int8_t)to_square;
    move.promotion = length == 5;
    return move;
}

void sr_move_usi(sr_move move, char *buffer) {
    if (move.from_square < 0) {
        buffer[0] = PIECE_CHARS[move.drop_piece_type];
        buffer[1] = '*';
        square_name(move.to_square, buffer + 2);
        return;
    }
    square_name(move.from_square, buffer);
    square_name(move.to_square, buffer + 2);
    if (move.promotion) {
        buffer[4] = '+';
        buffer[5] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Push / pop                                                          */
/* ------------------------------------------------------------------ */

static void add_hand(sr_board *board, int color, int piece_type) {
    int old_count = board->hands[color][piece_type];
    int new_count = old_count + 1;
    if (old_count > 0) {
        board->hash ^= hand_hash(color, piece_type, old_count);
    }
    board->hash ^= hand_hash(color, piece_type, new_count);
    board->hands[color][piece_type] = (uint8_t)new_count;
}

static void remove_hand(sr_board *board, int color, int piece_type) {
    int count = board->hands[color][piece_type];
    board->hash ^= hand_hash(color, piece_type, count);
    if (count > 1) {
        board->hash ^= hand_hash(color, piece_type, count - 1);
    }
    board->hands[color][piece_type] = (uint8_t)(count - 1);
}

/* Whether a move keeps the board whole. A null move has both squares -1 and
 * nothing else set. Any other move goes to a square on the board without
 * taking a king: a piece of the side to move from a square, taking no piece
 * of its own and promoting only if it can, or a piece in hand dropped onto
 * an empty square. */
static bool fits_board(const sr_board *board, sr_move move) {
    if (move.to_square == -1) {
        return move.from_square == -1 && !move.promotion && move.drop_piece_type == 0;
    }
    if (move.to_square < 0 || move.to_square >= SR_BOARD_SIZE) {
        return false;
    }
    int target = board->board[move.to_square];
    if (abs(target) == SR_KING) {
        return false;
    }
    if (move.from_square == -1) {
        int piece_type = move.drop_piece_type;
        return piece_type >= SR_PAWN && piece_type <= SR_ROOK && !move.promotion &&
               target == 0 && board->hands[board->turn][piece_type] > 0;
    }
    if (move.from_square < 0 || move.from_square >= SR_BOARD_SIZE ||
        move.drop_piece_type != 0) {
        return false;
    }
    int piece = board->board[move.from_square];
    if (piece == 0 || piece_color(piece) != board->turn ||
        (target != 0 && piece_color(target) == board->turn)) {
        return false;
    }
    return !move.promotion || promoted_of(abs(piece)) != 0;
}

/* Plays a move that fits the board; false only when the undo stack is
 * full. */
static bool push_move(sr_board *board, sr_move move) {
    if (board->undo_count >= MAX_UNDO) {
        return false;
    }
    int previous_turn = board->turn;
    int previous_move_number = board->move_number;
    uint64_t previous_hash = board->hash;

    sr_undo *undo = &board->undo_stack[board->undo_count];
    undo->move = move;
    undo->previous_turn = (uint8_t)previous_turn;
    undo->previous_move_number = previous_move_number;
    undo->previous_hash = previous_hash;

    if (move.from_square < 0 && move.to_square < 0) {
        /* Null move. */
        undo->moved_piece = 0;
        undo->captured_piece = 0;
        board->hash ^= ZOBRIST_TURN;
        board->turn = 1 - board->turn;
        board->move_number++;
        board->undo_count++;
        return true;
    }

    int captured_piece = board->board[move.to_square];
    int moved_piece;
    if (move.from_square < 0) {
        moved_piece = signed_piece(previous_turn, move.drop_piece_type);
        remove_hand(board, previous_turn, move.drop_piece_type);
        board->board[move.to_square] = (int8_t)moved_piece;
        board->hash ^= board_hash(moved_piece, move.to_square);
    } else {
        moved_piece = board->board[move.from_square];
        board->hash ^= board_hash(moved_piece, move.from_square);
        board->board[move.from_square] = 0;
        if (captured_piece) {
            board->hash ^= board_hash(captured_piece, move.to_square);
            add_hand(board, previous_turn, unpromoted_of(abs(captured_piece)));
        }
        int piece_type = move.promotion ? promoted_of(abs(moved_piece)) : abs(moved_piece);
        int new_piece = signed_piece(previous_turn, piece_type);
        board->board[move.to_square] = (int8_t)new_piece;
        board->hash ^= board_hash(new_piece, move.to_square);
        if (abs(moved_piece) == SR_KING) {
            board->king_squares[previous_turn] = move.to_square;
        }
    }
    board->hash ^= ZOBRIST_TURN;
    board->turn = 1 - board->turn;
    board->move_number++;
    undo->moved_piece = (int8_t)moved_piece;
    undo->captured_piece = (int8_t)captured_piece;
    board->undo_count++;
    return true;
}

bool sr_push(sr_board *board, sr_move move) {
    return fits_board(board, move) && push_move(board, move);
}

static bool same_move(sr_move a, sr_move b) {
    return a.from_square == b.from_square && a.to_square == b.to_square &&
           !a.promotion == !b.promotion && a.drop_piece_type == b.drop_piece_type;
}

bool sr_push_usi(sr_board *board, const char *usi) {
    sr_move move = sr_move_from_usi(usi);
    if (move.to_square < 0) {
        return false;
    }
    sr_move legal[SR_MAX_MOVES];
    int count = sr_legal_moves(board, legal);
    for (int i = 0; i < count; i++) {
        if (same_move(legal[i], move)) {
            return push_move(board, move);
        }
    }
    return false;
}

bool sr_pop(sr_board *board) {
    if (board->undo_count <= 0) {
        return false;
    }
    board->undo_count--;
    const sr_undo *undo = &board->undo_stack[board->undo_count];
    sr_move move = undo->move;
    board->turn = undo->previous_turn;
    board->move_number = undo->previous_move_number;
    if (move.from_square < 0 && move.to_square < 0) {
        board->hash = undo->previous_hash;
        return true;
    }
    if (move.from_square < 0) {
        board->board[move.to_square] = undo->captured_piece;
        board->hands[board->turn][move.drop_piece_type]++;
        board->hash = undo->previous_hash;
        return true;
    }
    board->board[move.from_square] = undo->moved_piece;
    board->board[move.to_square] = undo->captured_piece;
    if (abs(undo->moved_piece) == SR_KING) {
        board->king_squares[board->turn] = move.from_square;
    }
    if (undo->captured_piece) {
        board->hands[board->turn][unpromoted_of(abs(undo->captured_piece))]--;
    }
    board->hash = undo->previous_hash;
    return true;
}

/* ------------------------------------------------------------------ */
/* Attack detection                                                    */
/* ------------------------------------------------------------------ */

bool sr_is_attacked_by(const sr_board *board, int color, int square) {
    const sr_step_sources *steps = &STEP_ATTACK_SOURCES[color][square];
    for (int i = 0; i < steps->count; i++) {
        if (board->board[steps->entries[i].from_square] == steps->entries[i].piece) {
            return true;
        }
    }
    const sr_ray_sources *rays = &RAY_ATTACK_SOURCES[color][square];
    for (int i = 0; i < rays->count; i++) {
        const sr_ray_source *entry = &rays->entries[i];
        for (int j = 0; j < entry->ray.length; j++) {
            int found_piece = board->board[entry->ray.squares[j]];
            if (found_piece) {
                for (int k = 0; k < entry->piece_count; k++) {
                    if (found_piece == entry->pieces[k]) {
                        return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

static int attackers_by(const sr_board *board, int color, int square, int *out) {
    int count = 0;
    const sr_step_sources *steps = &STEP_ATTACK_SOURCES[color][square];
    for (int i = 0; i < steps->count; i++) {
        if (board->board[steps->entries[i].from_square] == steps->entries[i].piece) {
            out[count++] = steps->entries[i].from_square;
        }
    }
    const sr_ray_sources *rays = &RAY_ATTACK_SOURCES[color][square];
    for (int i = 0; i < rays->count; i++) {
        const sr_ray_source *entry = &rays->entries[i];
        for (int j = 0; j < entry->ray.length; j++) {
            int found_piece = board->board[entry->ray.squares[j]];
            if (found_piece) {
                for (int k = 0; k < entry->piece_count; k++) {
                    if (found_piece == entry->pieces[k]) {
                        out[count++] = entry->ray.squares[j];
                        break;
                    }
                }
                break;
            }
        }
    }
    return count;
}

bool sr_is_check(sr_board *board) {
    return sr_is_attacked_by(board, 1 - board->turn,
                             board->king_squares[board->turn]);
}

bool sr_is_fourfold_repetition(const sr_board *board) {
    /* The reference implementation stubs this to False. */
    (void)board;
    return false;
}

/* ------------------------------------------------------------------ */
/* Move generation                                                     */
/* ------------------------------------------------------------------ */

static bool in_promotion_zone(int square, int color) {
    int rank = square / 9;
    return color == SR_BLACK ? rank <= 2 : rank >= 6;
}

static bool can_move_without_promotion(int piece_type, int to_square, int color) {
    int rank = to_square / 9;
    if (piece_type == SR_PAWN || piece_type == SR_LANCE) {
        return rank != (color == SR_BLACK ? 0 : 8);
    }
    if (piece_type == SR_KNIGHT) {
        return color == SR_BLACK ? rank > 1 : rank < 7;
    }
    return true;
}

static void append_promotion_variants(sr_move *moves, int *count, int from_square,
                                      int to_square, int piece_type, int color) {
    sr_move base = {(int8_t)from_square, (int8_t)to_square, 0, 0};
    if (promoted_of(piece_type) == 0) {
        moves[(*count)++] = base;
        return;
    }
    bool can_promote =
        in_promotion_zone(from_square, color) || in_promotion_zone(to_square, color);
    if (!can_promote) {
        moves[(*count)++] = base;
        return;
    }
    sr_move promo = base;
    promo.promotion = 1;
    if (!can_move_without_promotion(piece_type, to_square, color)) {
        moves[(*count)++] = promo;
        return;
    }
    moves[(*count)++] = base;
    moves[(*count)++] = promo;
}

static int drop_moves(const sr_board *board, sr_move *moves) {
    int count = 0;
    int turn = board->turn;
    bool has_hand = false;
    for (int i = 0; i < NUM_DROP_PIECES; i++) {
        if (board->hands[turn][DROP_PIECES[i]] > 0) {
            has_hand = true;
            break;
        }
    }
    if (!has_hand) {
        return 0;
    }
    bool pawn_files[9] = {false};
    int pawn = signed_piece(turn, SR_PAWN);
    for (int square = 0; square < SR_BOARD_SIZE; square++) {
        if (board->board[square] == pawn) {
            pawn_files[square % 9] = true;
        }
    }
    for (int i = 0; i < NUM_DROP_PIECES; i++) {
        int piece_type = DROP_PIECES[i];
        if (board->hands[turn][piece_type] <= 0) {
            continue;
        }
        for (int to_square = 0; to_square < SR_BOARD_SIZE; to_square++) {
            if (board->board[to_square]) {
                continue;
            }
            if (!can_move_without_promotion(piece_type, to_square, turn)) {
                continue;
            }
            if (piece_type == SR_PAWN && pawn_files[to_square % 9]) {
                continue;
            }
            sr_move move = {-1, (int8_t)to_square, 0, (uint8_t)piece_type};
            moves[count++] = move;
        }
    }
    return count;
}

static int pseudo_legal_moves(const sr_board *board, sr_move *moves) {
    int count = 0;
    int turn = board->turn;
    bool turn_is_black = turn == SR_BLACK;
    for (int from_square = 0; from_square < SR_BOARD_SIZE; from_square++) {
        int piece = board->board[from_square];
        if (piece == 0 || (piece > 0) != turn_is_black) {
            continue;
        }
        int piece_type = abs(piece);
        const sr_square_rays *rays = &MOVE_RAYS[turn][piece_type][from_square];
        for (int r = 0; r < rays->ray_count; r++) {
            const sr_ray *ray = &rays->rays[r];
            for (int j = 0; j < ray->length; j++) {
                int to_square = ray->squares[j];
                int target = board->board[to_square];
                if (target && (target > 0) == turn_is_black) {
                    break;
                }
                if (abs(target) == SR_KING) {
                    break;
                }
                append_promotion_variants(moves, &count, from_square, to_square,
                                          piece_type, turn);
                if (target) {
                    break;
                }
            }
        }
    }
    count += drop_moves(board, moves + count);
    return count;
}

static int pseudo_noisy_moves(const sr_board *board, sr_move *moves) {
    int count = 0;
    int turn = board->turn;
    bool turn_is_black = turn == SR_BLACK;
    for (int from_square = 0; from_square < SR_BOARD_SIZE; from_square++) {
        int piece = board->board[from_square];
        if (piece == 0 || (piece > 0) != turn_is_black) {
            continue;
        }
        int piece_type = abs(piece);
        const sr_square_rays *rays = &MOVE_RAYS[turn][piece_type][from_square];
        for (int r = 0; r < rays->ray_count; r++) {
            const sr_ray *ray = &rays->rays[r];
            for (int j = 0; j < ray->length; j++) {
                int to_square = ray->squares[j];
                int target = board->board[to_square];
                if (target && (target > 0) == turn_is_black) {
                    break;
                }
                if (abs(target) == SR_KING) {
                    break;
                }
                if (target) {
                    append_promotion_variants(moves, &count, from_square, to_square,
                                              piece_type, turn);
                    break;
                }
                if (promoted_of(piece_type) != 0 &&
                    (in_promotion_zone(from_square, turn) ||
                     in_promotion_zone(to_square, turn))) {
                    sr_move move = {(int8_t)from_square, (int8_t)to_square, 1, 0};
                    moves[count++] = move;
                }
            }
        }
    }
    return count;
}

/* Squares strictly between king and checker along a line, or empty. */
static int block_squares_between(int king_square, int checker_square, int *out) {
    int king_file = king_square % 9;
    int king_rank = king_square / 9;
    int checker_file = checker_square % 9;
    int checker_rank = checker_square / 9;
    int file_delta = checker_file - king_file;
    int rank_delta = checker_rank - king_rank;
    int file_step;
    int rank_step;
    if (file_delta == 0) {
        file_step = 0;
    } else if (rank_delta == 0 || abs(file_delta) == abs(rank_delta)) {
        file_step = file_delta > 0 ? 1 : -1;
    } else {
        return 0;
    }
    if (rank_delta == 0) {
        rank_step = 0;
    } else if (file_delta == 0 || abs(file_delta) == abs(rank_delta)) {
        rank_step = rank_delta > 0 ? 1 : -1;
    } else {
        return 0;
    }
    if (file_step == 0 && rank_step == 0) {
        return 0;
    }
    int count = 0;
    int file_index = king_file + file_step;
    int rank = king_rank + rank_step;
    while (file_index != checker_file || rank != checker_rank) {
        out[count++] = rank * 9 + file_index;
        file_index += file_step;
        rank += rank_step;
    }
    return count;
}

static int pseudo_legal_moves_for_legality_check(sr_board *board, sr_move *moves) {
    int king_square = board->king_squares[board->turn];
    int attackers[16];
    int attacker_count = attackers_by(board, 1 - board->turn, king_square, attackers);
    int all_count = pseudo_legal_moves(board, moves);
    if (attacker_count == 0) {
        return all_count;
    }

    int checker_square = attackers[0];
    int block_squares[8];
    int block_count = block_squares_between(king_square, checker_square, block_squares);
    int kept = 0;
    for (int i = 0; i < all_count; i++) {
        sr_move move = moves[i];
        if (move.from_square == king_square) {
            moves[kept++] = move;
            continue;
        }
        if (attacker_count > 1) {
            continue;
        }
        bool blocks = move.to_square == checker_square;
        for (int j = 0; !blocks && j < block_count; j++) {
            blocks = move.to_square == block_squares[j];
        }
        if (blocks) {
            moves[kept++] = move;
        }
    }
    return kept;
}

static bool has_legal_evasion_without_pawn_drop_mate_check(sr_board *board) {
    sr_move responses[SR_MAX_MOVES];
    int count = pseudo_legal_moves(board, responses);
    for (int i = 0; i < count; i++) {
        if (!push_move(board, responses[i])) {
            continue;
        }
        bool safe = !sr_is_attacked_by(board, board->turn,
                                       board->king_squares[1 - board->turn]);
        sr_pop(board);
        if (safe) {
            return true;
        }
    }
    return false;
}

static bool is_illegal_pawn_drop_mate(sr_board *board, sr_move move) {
    if (move.from_square >= 0 || move.drop_piece_type != SR_PAWN) {
        return false;
    }
    if (!push_move(board, move)) {
        return true;
    }
    int defender = board->turn;
    bool illegal;
    if (!sr_is_attacked_by(board, 1 - defender, board->king_squares[defender])) {
        illegal = false;
    } else {
        illegal = !has_legal_evasion_without_pawn_drop_mate_check(board);
    }
    sr_pop(board);
    return illegal;
}

int sr_legal_moves(sr_board *board, sr_move *out) {
    sr_move pseudo[SR_MAX_MOVES];
    int pseudo_count = pseudo_legal_moves_for_legality_check(board, pseudo);
    int count = 0;
    for (int i = 0; i < pseudo_count; i++) {
        sr_move move = pseudo[i];
        if (is_illegal_pawn_drop_mate(board, move)) {
            continue;
        }
        if (!push_move(board, move)) {
            continue;
        }
        if (!sr_is_attacked_by(board, board->turn,
                               board->king_squares[1 - board->turn])) {
            out[count++] = move;
        }
        sr_pop(board);
    }
    return count;
}

bool sr_has_legal_move(sr_board *board) {
    sr_move pseudo[SR_MAX_MOVES];
    int pseudo_count = pseudo_legal_moves_for_legality_check(board, pseudo);
    for (int i = 0; i < pseudo_count; i++) {
        sr_move move = pseudo[i];
        if (is_illegal_pawn_drop_mate(board, move)) {
            continue;
        }
        if (!push_move(board, move)) {
            continue;
        }
        bool legal = !sr_is_attacked_by(board, board->turn,
                                        board->king_squares[1 - board->turn]);
        sr_pop(board);
        if (legal) {
            return true;
        }
    }
    return false;
}

bool sr_is_game_over(sr_board *board) { return !sr_has_legal_move(board); }

int sr_legal_tactical_moves(sr_board *board, sr_move *out,
                            bool include_quiet_checks) {
    sr_move pseudo[SR_MAX_MOVES];
    int pseudo_count = include_quiet_checks ? pseudo_legal_moves(board, pseudo)
                                            : pseudo_noisy_moves(board, pseudo);
    int count = 0;
    for (int i = 0; i < pseudo_count; i++) {
        sr_move move = pseudo[i];
        if (is_illegal_pawn_drop_mate(board, move)) {
            continue;
        }
        bool tactical =
            move.promotion || (move.to_square >= 0 && board->board[move.to_square] != 0);
        if (!tactical && !include_quiet_checks) {
            continue;
        }
        if (!push_move(board, move)) {
            continue;
        }
        bool keep = false;
        if (!sr_is_attacked_by(board, board->turn,
                               board->king_squares[1 - board->turn])) {
            keep = tactical || sr_is_check(board);
        }
        sr_pop(board);
        if (keep) {
            out[count++] = move;
        }
    }
    return count;
}
