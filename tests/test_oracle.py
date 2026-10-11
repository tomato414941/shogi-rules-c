"""Oracle tests: compare shogi-rules-cli against shogi-rules-python."""

from __future__ import annotations

from pathlib import Path
import random
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
REFERENCE_SRC = ROOT.parent / "shogi-rules-python" / "src"
sys.path.insert(0, str(REFERENCE_SRC))

from shogi_rules import BLACK, STARTPOS_SFEN, WHITE, Board, Move  # noqa: E402

REJECTED_SFENS = (
    "",
    "4k4/9/9/9/9/9/9/9/4K4 b",
    "4k4/9/9/9/9/9/9/9/4K4 b - 1 moves 7g7f",
    "kK b - 1",
    "4k4/9/9/9/9/9/9/9/4K4/ b - 1",
    "4k5/9/9/9/9/9/9/9/4K4 b - 1",
    "4k3/9/9/9/9/9/9/9/4K4 b - 1",
    "4k4/9/9/9/9/9/9/9/04K4 b - 1",
    "4k4/9/9/9/9/9/9/+1P7/4K4 b - 1",
    "4k4/9/9/9/9/9/9/+G8/4K4 b - 1",
    "4k4/9/9/9/9/9/9/8+/4K4 b - 1",
    "4k4/9/9/9/9/9/9/9/4K4 x - 1",
    "4k4/9/9/9/9/9/9/9/4K4 b 0P 1",
    "4k4/9/9/9/9/9/9/9/4K4 b 2PP 1",
    "4k4/9/9/9/9/9/9/9/4K4 b P2 1",
    "4k4/9/9/9/9/9/9/9/4K4 b K 1",
    "4k4/9/9/9/9/9/9/9/4K4 b 99999999999P 1",
    "4k4/9/9/9/9/9/9/9/4K4 b - 0",
    "4k4/9/9/9/9/9/9/9/4K4 b - -1",
    "4k4/9/9/9/9/9/9/9/4K4 b - 1000000001",
    "4k4/9/9/9/9/9/9/9/4K4 b - 99999999999",
    "4k4/9/9/9/9/9/9/9/K3K4 b - 1",
    "4k4/9/9/9/9/9/9/9/4K4 b 10P9p 1",
    "4k4/9/9/9/9/9/9/+R8/4K4 b 2R 1",
    "4k4/9/9/9/9/9/4P4/4P4/4K4 b - 1",
    "P3k4/9/9/9/9/9/9/9/4K4 b - 1",
    "4k4/N8/9/9/9/9/9/9/4K4 b - 1",
    "4k4/9/9/9/9/9/9/9/p3K4 b - 1",
    "4k4/9/9/9/9/9/9/4r4/4K4 w - 1",
)
READ_SFENS = (
    "4k4/9/9/9/9/9/9/9/4K4 b -",
    " 4k4/9/9/9/9/9/9/9/4K4 \t b  -  1 ",
    "4k4/45/9/9/9/9/9/9/4K4 b - 1",
    "4k4/9/9/9/9/9/9/9/4K4 b pR1P02s 007",
    "4k4/9/9/9/9/9/9/9/4K4 b 2R2B4G4S4N4L18P 1",
    "4k4/9/9/9/9/9/9/9/4K4 w - 1000000000",
    "4k4/9/9/9/9/9/9/4r4/4K4 b - 1",
    "+P3k3+L/+N8/9/9/9/9/4P4/4+P4/4K4 b - 1",
)
# Each position with a move that is malformed or not legal there.
REJECTED_USIS = (
    (STARTPOS_SFEN, ""),
    (STARTPOS_SFEN, "7g7fxyz"),
    (STARTPOS_SFEN, "7g7f++"),
    (STARTPOS_SFEN, "0g0f"),
    (STARTPOS_SFEN, "7j7f"),
    (STARTPOS_SFEN, "p*5e"),
    (STARTPOS_SFEN, "K*5e"),
    (STARTPOS_SFEN, "5e5d"),
    (STARTPOS_SFEN, "5a5b"),
    (STARTPOS_SFEN, "6i5h+"),
    (STARTPOS_SFEN, "2h2b"),
    (STARTPOS_SFEN, "7g7f+"),
    (STARTPOS_SFEN, "7g7e"),
    ("4k4/9/9/9/9/9/4P4/9/4K4 b P 1", "P*5e"),
    ("4k4/9/9/9/9/9/9/9/4K4 b P 1", "P*1a"),
    ("4k4/P8/9/9/9/9/9/9/4K4 b - 1", "9b9a"),
    ("k3r4/9/9/9/9/9/9/4G4/4K4 b - 1", "5h4h"),
    ("8k/9/6NG1/9/9/9/9/9/4K4 b P 1", "P*1b"),
)
SFEN_ALPHABET = "0123456789+-/ \tbwPLNSGBRKplnsgbrk"
USI_ALPHABET = "0123456789abcdefghij+*PLNSGBRKplnsgbrk"


def mutate(text: str, rng: random.Random, alphabet: str) -> str:
    chars = list(text)
    for _ in range(rng.randint(1, 3)):
        index = rng.randrange(len(chars) + 1)
        edit = rng.choice(("insert", "delete", "replace"))
        if edit == "insert" or index == len(chars):
            chars.insert(index, rng.choice(alphabet))
        elif edit == "delete":
            del chars[index]
        else:
            chars[index] = rng.choice(alphabet)
    return "".join(chars)


# Each start position with the moves before a cycle and the cycle that is played three times from there.
REPEATED_CYCLES = (
    (STARTPOS_SFEN, (), ("5i5h", "5a5b", "5h5i", "5b5a")),
    ("8k/9/7R1/9/9/9/9/9/K8 b - 1", (), ("2c1c", "1a2a", "1c2c", "2a1a")),
    ("8k/9/8p/7R1/9/9/9/9/K8 b - 1", ("2d2c", "1c1d"), ("2c1c", "1a2a", "1c2c", "2a1a")),
)


def outcome_text(board: Board) -> str:
    outcome = board.outcome()
    if outcome is None:
        return "ongoing -"
    winner = {BLACK: "black", WHITE: "white", None: "-"}[outcome.winner]
    return f"{outcome.termination.value} {winner}"


def find_cycle(board: Board) -> list[str] | None:
    # Two moves, one for each side, that the next two moves take back.
    def reversible(move: Move) -> bool:
        return move.from_square is not None and not move.promotion and board.board[move.to_square] == 0

    for first in [move for move in board.legal_moves if reversible(move)][:6]:
        board.push(first)
        for second in [move for move in board.legal_moves if reversible(move)][:6]:
            cycle = [first, second, Move(first.to_square, first.from_square), Move(second.to_square, second.from_square)]
            board.push(second)
            if cycle[2] in board.legal_moves:
                board.push(cycle[2])
                legal = cycle[3] in board.legal_moves
                board.pop()
                if legal:
                    board.pop()
                    board.pop()
                    return [move.usi() for move in cycle]
            board.pop()
        board.pop()
    return None


def playout_sfens(rng: random.Random, plies: int) -> list[str]:
    sfens = [STARTPOS_SFEN]
    reference = Board.startpos()
    for _ in range(plies):
        moves = sorted(reference.legal_move_usis())
        if not moves:
            break
        reference.push_usi(rng.choice(moves))
        sfens.append(reference.sfen())
    return sfens


class Cli:
    def __init__(self) -> None:
        self.process = subprocess.Popen(
            [str(ROOT / "shogi-rules-cli")],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
        )

    def command(self, line: str) -> str:
        assert self.process.stdin is not None and self.process.stdout is not None
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()
        return self.process.stdout.readline().rstrip("\n")

    def legal(self) -> set[str]:
        response = self.command("legal")
        return set(response.split()) if response else set()

    def tactical(self, include_quiet_checks: bool) -> set[str]:
        response = self.command(f"tactical {1 if include_quiet_checks else 0}")
        return set(response.split()) if response else set()

    def close(self) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write("quit\n")
        self.process.stdin.flush()
        self.process.wait(timeout=10)


class OracleTest(unittest.TestCase):
    cli: Cli

    @classmethod
    def setUpClass(cls) -> None:
        cls.cli = Cli()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.cli.close()

    def assert_position_matches(self, reference: Board) -> None:
        self.assertEqual(self.cli.command("boardsfen"), reference.sfen())
        self.assertEqual(self.cli.legal(), reference.legal_move_usis())
        self.assertEqual(int(self.cli.command("hash"), 16), reference.zobrist_hash())
        self.assertEqual(
            self.cli.command("ischeck"), "1" if reference.is_check() else "0"
        )

    def test_startpos_matches_reference(self) -> None:
        self.cli.command("startpos")
        self.assert_position_matches(Board.startpos())

    def test_random_playouts_match_reference(self) -> None:
        rng = random.Random(20260706)
        for playout in range(5):
            self.cli.command("startpos")
            reference = Board.startpos()
            plies = 0
            for _ in range(60):
                self.assert_position_matches(reference)
                moves = sorted(reference.legal_move_usis())
                if not moves:
                    break
                move = rng.choice(moves)
                self.assertEqual(self.cli.command(f"push {move}"), "ok", move)
                reference.push_usi(move)
                plies += 1
            self.assertGreater(plies, 0, f"playout {playout} made no moves")
            for _ in range(plies):
                self.assertEqual(self.cli.command("pop"), "ok")
                reference.pop()
            self.assert_position_matches(reference)

    def test_tactical_moves_match_reference(self) -> None:
        rng = random.Random(7)
        self.cli.command("startpos")
        reference = Board.startpos()
        for _ in range(30):
            for include_quiet_checks in (False, True):
                expected = {
                    move.usi()
                    for move in reference.legal_tactical_moves(
                        include_quiet_checks=include_quiet_checks
                    )
                }
                self.assertEqual(self.cli.tactical(include_quiet_checks), expected)
            moves = sorted(reference.legal_move_usis())
            if not moves:
                break
            move = rng.choice(moves)
            self.cli.command(f"push {move}")
            reference.push_usi(move)

    def test_perft_matches_reference_and_known_values(self) -> None:
        def reference_perft(board: Board, depth: int) -> int:
            if depth <= 0:
                return 1
            moves = board.legal_moves
            if depth == 1:
                return len(moves)
            nodes = 0
            for move in moves:
                board.push(move)
                nodes += reference_perft(board, depth - 1)
                board.pop()
            return nodes

        self.cli.command("startpos")
        reference = Board.startpos()
        for depth in (1, 2, 3):
            self.assertEqual(
                int(self.cli.command(f"perft {depth}")),
                reference_perft(reference, depth),
                f"perft {depth}",
            )
        # Known shogi perft values from the startpos.
        self.assertEqual(int(self.cli.command("perft 4")), 719731)

    def test_rejects_structurally_invalid_pushes(self) -> None:
        self.cli.command("startpos")
        self.assertEqual(self.cli.command("push P*5e"), "error")
        self.assertEqual(self.cli.command("push 5i5a"), "error")

    def test_rejects_the_sfens_the_reference_rejects(self) -> None:
        for sfen in REJECTED_SFENS:
            with self.subTest(sfen=sfen):
                with self.assertRaises(ValueError):
                    Board.from_sfen(sfen)
                self.assertEqual(self.cli.command("sfen " + sfen), "error")

    def test_reads_sfens_as_the_reference_reads_them(self) -> None:
        for sfen in READ_SFENS:
            with self.subTest(sfen=sfen):
                self.assertEqual(self.cli.command("sfen " + sfen), "ok")
                self.assertEqual(self.cli.command("boardsfen"), Board.from_sfen(sfen).sfen())

    def test_reads_mutated_sfens_as_the_reference_reads_them(self) -> None:
        rng = random.Random(20261011)
        bases = playout_sfens(rng, 80)
        accepted = 0
        for _ in range(4000):
            sfen = mutate(rng.choice(bases), rng, SFEN_ALPHABET)
            try:
                expected = Board.from_sfen(sfen).sfen()
            except ValueError:
                expected = None
            if expected is None:
                self.assertEqual(self.cli.command("sfen " + sfen), "error", sfen)
                continue
            accepted += 1
            self.assertEqual(self.cli.command("sfen " + sfen), "ok", sfen)
            self.assertEqual(self.cli.command("boardsfen"), expected, sfen)
        self.assertGreater(accepted, 0)

    def test_rejects_the_moves_the_reference_rejects(self) -> None:
        for sfen, usi in REJECTED_USIS:
            with self.subTest(sfen=sfen, usi=usi):
                reference = Board.from_sfen(sfen)
                with self.assertRaises(ValueError):
                    reference.push_usi(usi)
                self.assertEqual(self.cli.command("sfen " + sfen), "ok")
                self.assertEqual(self.cli.command("push " + usi), "error")
                self.assert_position_matches(reference)

    def test_plays_mutated_moves_as_the_reference_plays_them(self) -> None:
        rng = random.Random(20261012)
        played = 0
        for sfen in playout_sfens(rng, 60):
            reference = Board.from_sfen(sfen)
            self.assertEqual(self.cli.command("sfen " + sfen), "ok")
            moves = sorted(reference.legal_move_usis())
            for _ in range(40):
                usi = mutate(rng.choice(moves), rng, USI_ALPHABET) if moves else mutate("7g7f", rng, USI_ALPHABET)
                try:
                    reference.push_usi(usi)
                except ValueError:
                    self.assertEqual(self.cli.command("push " + usi), "error", (sfen, usi))
                    continue
                played += 1
                self.assertEqual(self.cli.command("push " + usi), "ok", (sfen, usi))
                self.assertEqual(self.cli.command("boardsfen"), reference.sfen(), (sfen, usi))
                self.assertEqual(self.cli.command("pop"), "ok")
                reference.pop()
        self.assertGreater(played, 0)

    def assert_outcome_matches(self, reference: Board) -> None:
        self.assertEqual(self.cli.command("outcome"), outcome_text(reference))
        self.assert_position_matches(reference)

    def test_ends_repeated_positions_as_the_reference_does(self) -> None:
        for sfen, before, cycle in REPEATED_CYCLES:
            with self.subTest(sfen=sfen):
                self.assertEqual(self.cli.command("sfen " + sfen), "ok")
                reference = Board.from_sfen(sfen)
                for usi in before + cycle * 3:
                    self.assertEqual(self.cli.command("push " + usi), "ok")
                    reference.push_usi(usi)
                    self.assert_outcome_matches(reference)
                self.assertIsNotNone(reference.outcome())

    def test_counts_no_position_before_a_null_move_as_the_reference_does(self) -> None:
        self.cli.command("startpos")
        reference = Board.startpos()
        shuffle = ("5i5h", "5a5b", "5h5i", "5b5a")
        for usi in shuffle * 2:
            self.cli.command("push " + usi)
            reference.push_usi(usi)
        for _ in range(2):
            self.assertEqual(self.cli.command("null"), "ok")
            reference.push(Move(None, None))
            self.assert_outcome_matches(reference)
        for usi in shuffle * 3:
            self.cli.command("push " + usi)
            reference.push_usi(usi)
            self.assert_outcome_matches(reference)
        for _ in range(len(shuffle) * 3 + 2):
            self.assertEqual(self.cli.command("pop"), "ok")
            reference.pop()
            self.assert_outcome_matches(reference)

    def test_ends_random_cycles_as_the_reference_does(self) -> None:
        rng = random.Random(20261013)
        cycles = 0
        for sfen in playout_sfens(rng, 120)[::8]:
            reference = Board.from_sfen(sfen)
            cycle = find_cycle(reference)
            if cycle is None:
                continue
            cycles += 1
            self.assertEqual(self.cli.command("sfen " + sfen), "ok")
            for usi in cycle * 3:
                self.assertEqual(self.cli.command("push " + usi), "ok")
                reference.push_usi(usi)
                self.assert_outcome_matches(reference)
            self.assertTrue(reference.is_fourfold_repetition())
        self.assertGreater(cycles, 0)

    def test_ends_a_position_without_legal_moves_as_the_reference_does(self) -> None:
        sfen = "4k4/4G4/4P4/9/9/9/9/9/4K4 w - 1"
        self.assertEqual(self.cli.command("sfen " + sfen), "ok")
        self.assert_outcome_matches(Board.from_sfen(sfen))

    def test_keeps_the_position_when_an_sfen_is_rejected(self) -> None:
        self.cli.command("startpos")
        self.cli.command("push 7g7f")
        reference = Board.startpos()
        reference.push_usi("7g7f")

        self.assertEqual(self.cli.command("sfen 4k4/9/9/9/9/9/9/9/K3K4 b - 1"), "error")

        self.assert_position_matches(reference)
        self.assertEqual(self.cli.command("pop"), "ok")


if __name__ == "__main__":
    unittest.main(verbosity=2)
