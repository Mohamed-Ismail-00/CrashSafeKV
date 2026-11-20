"""Edge and error cases for cskv.exe that go beyond the basic CLI tests."""
import random
import subprocess

from conftest import CSKV, run_cskv

OK = {"ok": True}


def test_ctrl_z_byte_does_not_end_the_session(start):
    """0x1A is "end of file" for Windows text-mode input; it must be treated
    as an ordinary byte, not close the engine mid-session."""
    eng = start()
    assert eng.send("SET k before\x1aafter") == OK
    assert eng.send("GET k") == {"ok": True, "value": "before\x1aafter"}
    assert eng.send("KEYS") == {"ok": True, "keys": ["k"]}


def test_lf_and_crlf_line_endings_are_both_accepted(start):
    eng = start()
    raw = eng.proc.stdin.buffer          # exact bytes, no newline translation
    for line in (b"SET a 1\n", b"SET b 2\r\n"):
        raw.write(line)
        raw.flush()
        assert eng.read() == OK
    assert eng.send("GET a") == {"ok": True, "value": "1"}
    assert eng.send("GET b") == {"ok": True, "value": "2"}


def test_utf8_bom_before_first_command_is_ignored(start):
    """PowerShell pipes and Notepad files may start with EF BB BF."""
    eng = start()
    raw = eng.proc.stdin.buffer
    raw.write(b"\xef\xbb\xbfSET a 1\n")
    raw.flush()
    assert eng.read() == OK
    assert eng.send("GET a") == {"ok": True, "value": "1"}


def test_human_mode_script_file_with_bom_and_crlf(data_dir):
    """A command script saved by Notepad (BOM + CRLF) piped in as raw bytes."""
    script = b"\xef\xbb\xbfSET name Ahmed\r\nGET name\r\nEXIT\r\n"
    result = subprocess.run([str(CSKV), "--data-dir", str(data_dir)],
                            input=script, capture_output=True, timeout=30)
    assert result.returncode == 0
    assert b'"Ahmed"' in result.stdout
    assert b"unknown command" not in result.stdout


def test_random_input_lines_never_crash_the_engine(start):
    """300 random lines (printable, control characters, non-ASCII): every
    line gets exactly one JSON reply and the engine stays alive."""
    rng = random.Random(2024)
    alphabet = [chr(c) for c in range(1, 128) if chr(c) not in "\r\n"] + list("éمرحبا€")
    words = ["SET", "GET", "DEL", "KEYS", "STATS", "set", "SNAPSHOT", "RESTORE", ""]
    eng = start()
    for _ in range(300):
        junk = "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 60)))
        line = f"{rng.choice(words)} {junk}" if rng.random() < 0.7 else junk
        if line.strip().upper().startswith(("EXIT", "QUIT")):
            continue
        reply = eng.send(line)
        assert isinstance(reply.get("ok"), bool), reply
    assert eng.proc.poll() is None
    assert eng.send("SET still alive") == OK


def test_sync_none_mode_persists_after_clean_exit(start):
    eng = start("--sync", "none")
    assert eng.send("SET fast mode") == OK
    eng.exit()
    assert start().send("GET fast") == {"ok": True, "value": "mode"}


def test_data_dir_that_is_a_file_gives_exit_code_2(tmp_path):
    not_a_dir = tmp_path / "file.txt"
    not_a_dir.write_text("I am a file")
    result = run_cskv("--json", "--data-dir", not_a_dir)
    assert result.returncode == 2
    assert '"error":"IO"' in result.stdout


def test_snapshot_into_missing_folder_fails_cleanly(start, tmp_path):
    eng = start()
    eng.send("SET a 1")
    reply = eng.send(f"SNAPSHOT {tmp_path / 'no' / 'such' / 'dir' / 'x.snap'}")
    assert reply["ok"] is False and reply["error"] == "IO"
    assert eng.send("GET a") == {"ok": True, "value": "1"}


def test_restore_of_a_log_file_is_rejected(start, data_dir):
    """Pointing RESTORE at the wrong kind of file (our own log) is refused."""
    eng = start()
    eng.send("SET a 1")
    reply = eng.send(f"RESTORE {data_dir / 'cskv.log'}")
    assert reply["error"] == "CORRUPT"


def test_end_of_input_closes_cleanly_and_keeps_data(start):
    eng = start()
    eng.send("SET k v")
    eng.proc.stdin.close()             # like Ctrl+Z / closed pipe
    assert eng.proc.wait(timeout=10) == 0
    assert start().send("GET k") == {"ok": True, "value": "v"}
