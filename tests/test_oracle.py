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

from shogi_rules import Board  # noqa: E402


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
