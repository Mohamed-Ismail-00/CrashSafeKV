"""Runs build/cskv.exe --json and talks to it over pipes (CLAUDE.md 9.1).

Protocol: one command line in, exactly one JSON line out, in order. The first
line after start-up is the recovery report (or an open error).

Threading: a background thread only reads stdout and puts lines on a queue.
Everything else - including every callback - runs on the Tk thread when the
GUI calls poll(), so the interface never freezes and needs no locks.
"""
import json
import queue
import subprocess
import threading
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CSKV_EXE = ROOT / "build" / "cskv.exe"
CORRUPT_EXE = ROOT / "build" / "cskv_corrupt.exe"

# Keep a console window from flashing up when launched from the GUI.
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class EngineError(Exception):
    """The engine could not be started."""


class EngineClient:
    def __init__(self):
        self._proc = None
        self._events = queue.Queue()      # (proc, kind, payload) from readers
        self._pending = deque()           # reply callbacks, oldest first
        self._awaiting_startup = False
        self._on_startup = None
        self._on_exit = None

    # ---- state -----------------------------------------------------------

    @property
    def running(self):
        return self._proc is not None and self._proc.poll() is None

    @property
    def pid(self):
        return self._proc.pid if self._proc else None

    # ---- lifecycle -------------------------------------------------------

    def start(self, data_dir, on_startup, on_exit):
        """on_startup(msg): first JSON line. on_exit(code, stderr): the engine
        died on its own (stop()/kill() never trigger it)."""
        if self.running:
            raise EngineError("The engine is already running.")
        if not CSKV_EXE.exists():
            raise EngineError(f"{CSKV_EXE.name} was not found in build/. "
                              "Build the project with 'make' first.")
        try:
            proc = subprocess.Popen(
                [str(CSKV_EXE), "--json", "--data-dir", str(data_dir)],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, encoding="utf-8",
                errors="replace", bufsize=1, creationflags=_NO_WINDOW)
        except OSError as exc:
            raise EngineError(f"Could not start {CSKV_EXE.name}: {exc}") from exc

        self._proc = proc
        self._pending.clear()
        self._awaiting_startup = True
        self._on_startup, self._on_exit = on_startup, on_exit
        threading.Thread(target=self._read_loop, args=(proc,), daemon=True).start()

    def stop(self, timeout=3.0):
        """Clean shutdown (EXIT), falling back to kill after `timeout`."""
        proc = self._detach()
        if proc is None or proc.poll() is not None:
            return
        try:
            proc.stdin.write("EXIT\n")
            proc.stdin.flush()
            proc.wait(timeout)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()
            proc.wait()

    def kill(self):
        """Hard stop with no clean shutdown - exactly like a crash."""
        proc = self._detach()
        if proc is not None and proc.poll() is None:
            proc.kill()
            proc.wait()

    def _detach(self):
        proc, self._proc = self._proc, None
        self._pending.clear()
        self._awaiting_startup = False
        return proc

    # ---- commands --------------------------------------------------------

    def send(self, line, callback):
        """Queue one command; callback(reply_dict) runs on the Tk thread.
        Returns False if the engine is not running."""
        if not self.running:
            return False
        try:
            self._proc.stdin.write(line + "\n")
            self._proc.stdin.flush()
        except OSError:
            return False
        self._pending.append(callback)
        return True

    def poll(self, limit=500):
        """Deliver queued replies/events. Call regularly from the Tk loop."""
        for _ in range(limit):
            try:
                proc, kind, payload = self._events.get_nowait()
            except queue.Empty:
                return
            if proc is not self._proc:
                continue                  # left over from a stopped process
            if kind == "line":
                self._dispatch(payload)
            else:
                code, stderr = payload
                self._detach()
                self._on_exit(code, stderr)

    def _dispatch(self, line):
        try:
            msg = json.loads(line)
        except ValueError:
            msg = {"ok": False, "error": "PROTOCOL",
                   "message": f"Unreadable reply from engine: {line[:80]!r}"}
        if self._awaiting_startup:
            self._awaiting_startup = False
            self._on_startup(msg)
        elif self._pending:
            self._pending.popleft()(msg)

    def _read_loop(self, proc):
        try:
            for line in proc.stdout:
                self._events.put((proc, "line", line))
        except (OSError, ValueError):
            pass
        code = proc.wait()
        try:
            stderr = proc.stderr.read()
        except (OSError, ValueError):
            stderr = ""
        self._events.put((proc, "exit", (code, stderr)))


def run_corrupt(log_file, mode, amount):
    """Run cskv_corrupt.exe. Returns (success, message)."""
    if not CORRUPT_EXE.exists():
        return False, f"{CORRUPT_EXE.name} was not found. Build it with 'make'."
    try:
        result = subprocess.run(
            [str(CORRUPT_EXE), str(log_file), mode, str(amount)],
            capture_output=True, text=True, timeout=15, creationflags=_NO_WINDOW)
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, str(exc)
    message = (result.stdout or result.stderr).strip()
    return result.returncode == 0, message
