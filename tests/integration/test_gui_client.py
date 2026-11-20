"""Tests for the GUI's non-visual parts: input rules and the engine client.

These run without opening a window. The client is driven against the real
build/cskv.exe, exactly as gui/app.py uses it.
"""
import subprocess
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "gui"))

import rules  # noqa: E402
from engine_client import EngineClient, run_corrupt  # noqa: E402


# ---- rules.py ---------------------------------------------------------------

@pytest.mark.parametrize("key, problem", [
    ("", "Enter a key"),
    ("has space", "spaces"),
    ("tab\there", "spaces"),
    ("café", "printable ASCII"),
    ("k" * 257, "too long"),
])
def test_bad_keys(key, problem):
    assert problem in rules.check_key(key)


def test_good_keys():
    assert rules.check_key("user:42") is None
    assert rules.check_key("k" * 256) is None


@pytest.mark.parametrize("value, problem", [
    ("", "Enter a value"),
    ("two\nlines", "line breaks"),
    (" leading", "start with a space"),
    ("bell\x07", "control characters"),
    ("v" * (1024 * 1024 + 1), "too large"),
])
def test_bad_values(value, problem):
    assert problem in rules.check_value(value)


def test_good_values():
    assert rules.check_value("Ahmed Ali") is None
    assert rules.check_value("مرحبا tab\tok") is None
    assert rules.check_value("v" * (1024 * 1024)) is None


def test_fmt_bytes():
    assert rules.fmt_bytes(512) == "512 B"
    assert rules.fmt_bytes(2048) == "2.0 KB"
    assert rules.fmt_bytes(3 * 1024 * 1024) == "3.00 MB"


# ---- engine_client.py -------------------------------------------------------

class Recorder:
    """Collects what the client delivers through its callbacks."""

    def __init__(self):
        self.startup = None
        self.exit = None
        self.replies = []

    def on_startup(self, msg):
        self.startup = msg

    def on_exit(self, code, stderr):
        self.exit = (code, stderr)


def pump(client, done, timeout=10.0):
    """Call client.poll() (like the Tk loop does) until done() is true."""
    end = time.monotonic() + timeout
    while not done():
        assert time.monotonic() < end, "timed out waiting for the engine"
        client.poll()
        time.sleep(0.01)


@pytest.fixture
def client():
    c = EngineClient()
    yield c
    c.kill()


def test_client_start_send_and_replies_in_order(client, data_dir):
    rec = Recorder()
    client.start(data_dir, rec.on_startup, rec.on_exit)
    pump(client, lambda: rec.startup is not None)
    assert rec.startup["event"] == "recovery"

    for cmd in ("SET a 1", "SET b 2", "GET a", "KEYS"):
        assert client.send(cmd, rec.replies.append)
    pump(client, lambda: len(rec.replies) == 4)
    assert rec.replies[2] == {"ok": True, "value": "1"}
    assert rec.replies[3] == {"ok": True, "keys": ["a", "b"]}


def test_client_stop_is_clean_and_data_persists(client, data_dir):
    rec = Recorder()
    client.start(data_dir, rec.on_startup, rec.on_exit)
    client.send("SET k v", rec.replies.append)
    pump(client, lambda: rec.replies)
    client.stop()
    assert not client.running
    assert rec.exit is None                  # a requested stop is not an "exit"

    rec2 = Recorder()
    client.start(data_dir, rec2.on_startup, rec2.on_exit)
    pump(client, lambda: rec2.startup is not None)
    assert rec2.startup["records"] == 1


def test_client_reports_unexpected_exit(client, data_dir):
    rec = Recorder()
    client.start(data_dir, rec.on_startup, rec.on_exit)
    pump(client, lambda: rec.startup is not None)
    subprocess.run(["taskkill", "/F", "/PID", str(client.pid)],
                   capture_output=True, check=False)
    pump(client, lambda: rec.exit is not None)
    assert not client.running
    assert client.send("KEYS", rec.replies.append) is False


def test_client_reports_open_error_when_locked(client, data_dir):
    first, second = EngineClient(), client
    rec1, rec2 = Recorder(), Recorder()
    first.start(data_dir, rec1.on_startup, rec1.on_exit)
    pump(first, lambda: rec1.startup is not None)
    try:
        second.start(data_dir, rec2.on_startup, rec2.on_exit)
        pump(second, lambda: rec2.exit is not None)
        assert rec2.startup["error"] == "LOCKED"
        assert rec2.exit[0] == 2
    finally:
        first.stop()


def test_run_corrupt_reports_failures(data_dir):
    ok, message = run_corrupt(data_dir / "missing.log", "flip", 0)
    assert ok is False and "cannot open" in message
