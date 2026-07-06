# shogi-rules-c

C implementation of the rules of shogi: board state, legal move generation,
and game termination. MIT licensed.

This is a port of [`shogi-rules-python`](../shogi-rules-python), which serves
as the reference implementation and test oracle. No GPL shogi code is
referenced or ported.

Scope, mirroring the Python reference:

- board state (pieces, hands, side to move)
- legal move generation and position transitions (push / pop)
- game termination (checkmate, fourfold repetition, illegal pawn-drop mate)
- SFEN and USI move notation

## Layout

- `include/shogi_rules.h` — public API
- `src/` — implementation
- `src/cli.c` — line-oriented CLI used by the oracle tests
- `tests/` — C unit tests and Python oracle tests that compare against
  `shogi-rules-python`

## Build and test

```sh
make          # builds libshogirules.a and the shogi-rules-cli binary
make test     # C unit tests + oracle tests (needs ../shogi-rules-python)
```
