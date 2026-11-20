"""Shared fixtures: start build/cskv.exe in --json mode and talk to it.

Run `make` first; these tests drive the real executables.
"""
import json
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
CSKV = ROOT / "build" / "cskv.exe"
CORRUPT = ROOT / "build" / "cskv_corrupt.exe"


class Engine:
    """One running cskv.exe --json process."""

    def __init__(self, data_dir, *extra_args):
        # cwd = the test's temp folder, so relative paths (e.g. a random
        # "SNAPSHOT name" in the fuzz test) never land in the project folder.
        Path(data_dir).parent.mkdir(parents=True, exist_ok=True)
        self.proc = subprocess.Popen(
            [str(CSKV), "--json", "--data-dir", str(data_dir), *extra_args],
            cwd=str(Path(data_dir).parent),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )
        self.recovery = self.read()

    def read(self):
        line = self.proc.stdout.readline()
        assert line, f"engine produced no output (exit={self.proc.poll()})"
        return json.loads(line)

    def write(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def send(self, line):
        self.write(line)
        return self.read()

    def kill(self):
        """Hard stop, like a crash: no clean shutdown."""
        self.proc.kill()
        self.proc.wait()

    def exit(self):
        assert self.send("EXIT") == {"ok": True}
        assert self.proc.wait(timeout=10) == 0


@pytest.fixture(scope="session", autouse=True)
def built_executables():
    missing = [p.name for p in (CSKV, CORRUPT) if not p.exists()]
    if missing:
        pytest.fail(f"missing {missing}: run `make` first")


@pytest.fixture
def data_dir(tmp_path):
    return tmp_path / "data"


@pytest.fixture
def start(data_dir):
    """start() -> Engine on the test's data dir; leftovers are killed."""
    engines = []

    def _start(*extra_args, directory=None):
        engine = Engine(directory or data_dir, *extra_args)
        engines.append(engine)
        return engine

    yield _start
    for engine in engines:
        if engine.proc.poll() is None:
            engine.kill()


def corrupt(log_file, mode, arg):
    """Run cskv_corrupt.exe and return its CompletedProcess."""
    return subprocess.run(
        [str(CORRUPT), str(log_file), mode, str(arg)],
        capture_output=True, text=True, timeout=30,
    )


def run_cskv(*args, stdin_text=""):
    """Run cskv.exe to completion (for exit-code / human-mode tests)."""
    return subprocess.run(
        [str(CSKV), *map(str, args)],
        input=stdin_text, capture_output=True, text=True, timeout=30,
    )
