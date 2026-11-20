"""Integration tests: drive the real cskv.exe / cskv_corrupt.exe (CLAUDE.md 10.2)."""
import threading

import pytest

from conftest import corrupt, run_cskv

OK = {"ok": True}


def error_code(response):
    assert response["ok"] is False, response
    return response["error"]


# ---- normal use -------------------------------------------------------------

def test_fresh_store_reports_empty_recovery(start, data_dir):
    eng = start()
    assert eng.recovery["event"] == "recovery"
    assert eng.recovery["records"] == 0
    assert eng.recovery["corruption"] is False
    assert (data_dir / "cskv.log").exists()      # missing data dir was created


def test_set_get_del_keys(start):
    eng = start()
    assert eng.send("SET name Ahmed Ali") == OK
    assert eng.send("SET age 21") == OK
    assert eng.send("GET name") == {"ok": True, "value": "Ahmed Ali"}
    assert eng.send("KEYS") == {"ok": True, "keys": ["age", "name"]}
    assert eng.send("DEL age") == OK
    assert error_code(eng.send("GET age")) == "NOT_FOUND"
    assert error_code(eng.send("DEL age")) == "NOT_FOUND"
    assert eng.send("KEYS") == {"ok": True, "keys": ["name"]}


def test_commands_are_case_insensitive(start):
    eng = start()
    assert eng.send("set k v") == OK
    assert eng.send("Get k") == {"ok": True, "value": "v"}


def test_value_spaces_and_json_escaping(start):
    eng = start()
    value = 'say "hi" \\ to\tall   مرحبا'
    assert eng.send(f"SET msg {value}") == OK
    assert eng.send("GET msg") == {"ok": True, "value": value}


def test_stats_and_compact(start):
    eng = start()
    for cmd in ["SET a 1", "SET b 2", "SET c 3", "SET d 4", "SET e 5",
                "SET a one", "SET b two", "DEL e"]:
        assert eng.send(cmd) == OK
    stats = eng.send("STATS")["stats"]
    assert stats["live_keys"] == 4
    assert stats["dead_records"] == 4

    result = eng.send("COMPACT")
    assert result["ok"] is True
    assert result["bytes_after"] < result["bytes_before"]
    stats = eng.send("STATS")["stats"]
    assert stats["dead_records"] == 0
    assert stats["log_bytes"] == result["bytes_after"]


def test_snapshot_and_restore(start, tmp_path):
    snap = tmp_path / "backup.snap"
    eng = start()
    eng.send("SET a 1")
    eng.send("SET b 2")
    assert eng.send(f"SNAPSHOT {snap}") == OK
    eng.send("SET c 3")
    eng.send("DEL a")
    assert eng.send(f"RESTORE {snap}") == OK
    assert eng.send("KEYS") == {"ok": True, "keys": ["a", "b"]}


def test_corrupted_snapshot_leaves_state_untouched(start, tmp_path):
    snap = tmp_path / "backup.snap"
    eng = start()
    eng.send("SET a 1")
    eng.send(f"SNAPSHOT {snap}")
    eng.send("SET b 2")
    assert corrupt(snap, "flip", 18).returncode == 0
    assert error_code(eng.send(f"RESTORE {snap}")) == "CORRUPT"
    assert eng.send("KEYS") == {"ok": True, "keys": ["a", "b"]}
    assert error_code(eng.send(f"RESTORE {tmp_path / 'nope.snap'}")) == "IO"


def test_restart_persistence(start):
    eng = start()
    eng.send("SET city Cairo")
    eng.exit()
    eng = start()
    assert eng.recovery["records"] == 1
    assert eng.send("GET city") == {"ok": True, "value": "Cairo"}


def test_help(start):
    response = start().send("HELP")
    assert response["ok"] is True and "SNAPSHOT" in response["help"]


# ---- invalid input ----------------------------------------------------------

@pytest.mark.parametrize("line, message_part", [
    ("", "empty command"),
    ("FOO bar", "unknown command"),
    ("SET", "missing key"),
    ("SET onlykey", "usage: SET"),
    ("GET", "missing key"),
    ("GET a b", "usage: GET"),
    ("DEL", "missing key"),
    ("KEYS extra", "usage: KEYS"),
    ("SNAPSHOT", "usage: SNAPSHOT"),
    ("RESTORE   ", "usage: RESTORE"),
    ("SET " + "k" * 257 + " v", "key too long"),
    ("SET bad\x01key v", "printable ASCII"),
])
def test_invalid_input_rejected(start, line, message_part):
    eng = start()
    response = eng.send(line)
    assert error_code(response) == "INVALID_ARG"
    assert message_part in response["message"]
    assert eng.send("KEYS") == {"ok": True, "keys": []}   # nothing was written


def test_max_key_and_value_accepted(start):
    eng = start()
    key, value = "k" * 256, "v" * (1024 * 1024)
    assert eng.send(f"SET {key} {value}") == OK
    assert eng.send(f"GET {key}")["value"] == value
    response = eng.send(f"SET big {value}x")
    assert "value too large" in response["message"]


def test_very_long_line_is_rejected_and_engine_continues(start):
    eng = start()
    response = eng.send("SET k " + "x" * (1024 * 1024 + 400))
    assert response == {"ok": False, "error": "INVALID_ARG",
                        "message": "line too long"}
    assert eng.send("SET after ok") == OK
    assert eng.send("KEYS") == {"ok": True, "keys": ["after"]}


# ---- crashes and corruption ---------------------------------------------------

def test_kill_during_writes_keeps_acknowledged_data(start):
    """Stream 1000 SETs, kill the engine while it is still writing, restart:
    every SET that was answered with OK must still be there."""
    eng = start()

    def writer():
        try:
            for i in range(1000):
                eng.write(f"SET key{i:04d} value-{i}")
        except OSError:
            pass                       # the pipe closes when we kill the engine

    thread = threading.Thread(target=writer)
    thread.start()
    acked = 0
    while acked < 600:
        assert eng.read() == OK
        acked += 1
    eng.kill()
    thread.join()

    eng = start()
    keys = eng.send("KEYS")["keys"]
    assert len(keys) >= acked
    for i in range(acked):
        assert eng.send(f"GET key{i:04d}") == {"ok": True, "value": f"value-{i}"}


@pytest.mark.parametrize("mode, arg, reason", [
    ("truncate", 5, "truncated body"),
    ("garbage", 40, None),                 # random bytes: any rule may fire
])
def test_tail_corruption_is_detected_and_cut(start, data_dir, mode, arg, reason):
    eng = start()
    for i in range(5):
        eng.send(f"SET k{i} v{i}")
    eng.kill()

    tool = corrupt(data_dir / "cskv.log", mode, arg)
    assert tool.returncode == 0, tool.stderr

    eng = start()
    rec = eng.recovery
    assert rec["corruption"] is True
    assert rec["bytes_discarded"] > 0
    if reason:
        assert rec["reason"].startswith(reason)
    expected_keys = 4 if mode == "truncate" else 5
    assert eng.send("KEYS")["keys"] == [f"k{i}" for i in range(expected_keys)]
    assert eng.send("SET new works") == OK          # writes continue normally
    eng.exit()
    assert start().recovery["corruption"] is False


def test_flipped_byte_drops_that_record_and_later_ones(start, data_dir):
    eng = start()
    for i in range(5):
        eng.send(f"SET k{i} v{i}")          # each record is 21 bytes
    eng.kill()
    assert corrupt(data_dir / "cskv.log", "flip", 8 + 21 * 2 + 18).returncode == 0

    eng = start()
    assert eng.recovery["reason"] == "CRC mismatch at offset 50"
    assert eng.send("KEYS")["keys"] == ["k0", "k1"]


def test_corrupt_tool_rejects_bad_input(data_dir, start):
    start().exit()
    log = data_dir / "cskv.log"
    assert corrupt(log, "flip", 10_000).returncode == 2
    assert corrupt(log, "truncate", 10_000).returncode == 2
    assert corrupt(log, "explode", 1).returncode == 1
    assert corrupt(data_dir / "missing.log", "flip", 0).returncode == 2


# ---- process level ------------------------------------------------------------

def test_second_engine_on_same_dir_is_locked(start, data_dir):
    first = start()
    second = run_cskv("--json", "--data-dir", data_dir)
    assert second.returncode == 2
    assert '"error":"LOCKED"' in second.stdout
    first.exit()


def test_bad_arguments_exit_code_1():
    assert run_cskv("--sync", "sometimes").returncode == 1
    assert run_cskv("--unknown").returncode == 1
    assert run_cskv("--data-dir").returncode == 1


def test_bad_format_exit_code_2(data_dir):
    data_dir.mkdir(parents=True)
    (data_dir / "cskv.log").write_bytes(b"NOT A CSKV LOG")
    result = run_cskv("--data-dir", data_dir)
    assert result.returncode == 2
    assert "format" in result.stderr


def test_human_mode(data_dir):
    result = run_cskv("--data-dir", data_dir,
                      stdin_text="SET name Ahmed\nGET name\nGET nope\nBAD\nEXIT\n")
    assert result.returncode == 0
    assert "Recovered 0 records" in result.stdout
    assert '"Ahmed"' in result.stdout
    assert "(nil)" in result.stdout
    assert "ERR unknown command" in result.stdout
