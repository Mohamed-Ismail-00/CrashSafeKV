# CrashSafeKV — A Durable Append-Only Key-Value Storage Engine

> **Draft report.** Every number and claim below comes from the code, the GUI, or
> `docs/test_results.txt`. Fill in the items marked **[TODO]** (names, IDs, course
> details), paste into Word / Google Docs, insert the figures, and export to PDF.

---

## 1. Title page

| | |
|---|---|
| Project | **CrashSafeKV** — A Durable Append-Only Key-Value Storage Engine |
| Catalog project | **#53 — Append-Only Key-Value Log** (Intermediate) |
| Integrated extensions | #66 Checksummed WAL Appender, #69 Log Compactor, #58 Key-Value Snapshot Tool |
| Course | Systems Programming — **[TODO: course code, instructor, semester]** |

### Team members and responsibilities

| # | Name | Student ID | Responsibility | Main files |
|---|---|---|---|---|
| 1 | [TODO] | [TODO] | Hash table and memory ownership | `src/hashtable.c` |
| 2 | [TODO] | [TODO] | CRC32 and record encode/decode | `src/crc32.c`, `src/record.c` |
| 3 | [TODO] | [TODO] | Log append and platform layer (durability, lock) | `src/log.c`, `src/platform.c` |
| 4 | [TODO] | [TODO] | Recovery and corruption handling | `src/recovery.c` |
| 5 | [TODO] | [TODO] | Compaction, snapshot and restore | `src/compact.c`, `src/snapshot.c` |
| 6 | [TODO] | [TODO] | CLI (human and JSON modes), error messages | `cli/` |
| 7 | [TODO] | [TODO] | GUI (Tkinter and engine client) | `gui/` |
| 8 | [TODO] | [TODO] | Corruption tool and crash/kill integration tests | `tools/`, `tests/integration/` |
| 9 | [TODO] | [TODO] | Unit tests, Makefile, memory checking | `tests/`, `Makefile` |
| 10 | [TODO] | [TODO] | Report, README, diagrams, screenshots | `docs/`, `README.md` |

---

## 2. Project overview

**Problem.** A program that keeps data in a file can lose or corrupt it when it crashes, is
killed, or loses power in the middle of a write. Editing a file in place makes this worse: a
half-finished update can destroy data that was already safe.

**Purpose.** CrashSafeKV stores key → value pairs (for example `name → Ahmed`) so that:

1. data that was acknowledged with OK is never lost,
2. damaged data is detected with CRC32 checksums and never loaded,
3. the store recovers automatically to the last consistent state on startup,
4. the file stays small over time through compaction,
5. complete, verified backups can be taken and restored.

**How it works.** Every change is *appended* to the end of a log file, together with a
checksum; the file is never edited in the middle. On startup the log is replayed from the
beginning to rebuild an in-memory hash table. If the last record was only half written, its
checksum (or length) does not match, so replay stops there, everything before it is kept, and
the damaged tail is cut off.

**Scope.** A single-process storage engine in C11 for Windows, a command-line program
(`cskv.exe`), a corruption tool for testing, and a Tkinter GUI that drives the real engine.

**Intended users.** Students and instructors studying durability and crash recovery; the GUI
is designed to *demonstrate* a crash and the recovery live.

---

## 3. Requirements and use cases

### 3.1 Functional requirements (all implemented)

| ID | Requirement | Where verified |
|---|---|---|
| FR-1 | `SET key value` stores or overwrites | `test_kv.c`, `test_cli.py` |
| FR-2 | `GET key` returns the value or "not found" | `test_kv.c`, `test_cli.py` |
| FR-3 | `DEL key` appends a tombstone; deleting a missing key writes nothing | `test_del_missing_writes_nothing` |
| FR-4 | `KEYS` lists live keys, sorted | `test_keys_sorted` |
| FR-5 | Every SET/DEL is durable before OK (sync = always) | `test_kill_during_writes_keeps_acknowledged_data` |
| FR-6 | Log replayed on startup | `test_recovery.c` |
| FR-7 | Bad CRC / length / type / truncated body detected; earlier records kept | 7 rule tests in `test_record.c`, `test_flip_every_byte` |
| FR-8 | Corrupted tail truncated; discarded bytes reported | `test_truncated_*`, `test_garbage_appended` |
| FR-9 | `COMPACT` rewrites live keys, atomic replace | `test_compact.c` |
| FR-10 | `SNAPSHOT` writes a checksummed, atomic snapshot | `test_snapshot.c` |
| FR-11 | `RESTORE` validates fully before replacing state | `test_corrupted_snapshot_rejected`, `test_random_snapshots_rejected` |
| FR-12 | `STATS` reports keys, size, records, dead records, last recovery | `test_stats_count_dead_records` |
| FR-13 | Invalid input rejected with a clear message | 12 cases in `test_invalid_input_rejected` |
| FR-14 | One process per data directory (lock file) | `test_second_open_is_locked`, `test_second_engine_on_same_dir_is_locked` |

### 3.2 Non-functional requirements

| ID | Requirement | Result |
|---|---|---|
| NFR-1 | Zero warnings with `-std=c11 -Wall -Wextra -Wpedantic -Werror` | Met (build log in `docs/test_results.txt`) |
| NFR-2 | Zero leaks under Dr. Memory | Dr. Memory cannot run on the test machine (section 7.5); the replacement leak check reports **0 leaks** |
| NFR-3 | `GET` O(1) average | Hash table, FNV-1a, load factor ≤ 0.75 |
| NFR-4 | Recover 100,000 records in under 2 s | **68.4 ms** for 100,000 records (4.4 MB) |
| NFR-5 | No crash on any input, including garbage files | 350+ random/damaged files and 300 random input lines (section 7) |
| NFR-6 | Windows calls isolated in `platform.c` | Only `src/platform.c` includes `windows.h` / `io.h` |

### 3.3 Limits

Keys 1–256 bytes, values 0–1 MiB (`CSKV_MAX_KEY_LEN`, `CSKV_MAX_VALUE_LEN`). On the command
line, keys are printable ASCII without spaces; the value is the rest of the line.

### 3.4 Main user scenario (demo, spec 9.4)

1. Start the engine on an empty data directory → recovery reports 0 records.
2. Set 5 keys, overwrite 2, delete 1 → the table shows 4 keys, stats show 4 dead records.
3. Compact → log shrinks from 221 B to 128 B (42 % smaller), dead records = 0.
4. Save a snapshot `backup.snap`.
5. Set 2 more keys.
6. Simulate a crash (kill + cut the last 5 bytes) → the engine restarts; the banner turns amber:
   "Corruption detected — discarded 21 bytes"; the last key is gone, all earlier keys are intact.
7. Restore `backup.snap` → the table matches step 4 exactly.

### 3.5 Out of scope

Networking, multi-process concurrent access, multi-threading, multi-key transactions,
encryption, range queries, Linux/macOS support.

---

## 4. Design and architecture

**Figure 1 — `docs/diagrams/architecture.svg`.**

### 4.1 Modules

| Module | Responsibility |
|---|---|
| `platform.c` | The only Windows-specific code: `_commit`, `_chsize_s`, `MoveFileExA`, lock file, directories, timer |
| `crc32.c` | Table-driven CRC32 (IEEE 802.3, polynomial `0xEDB88320`) |
| `record.c` | Encode a record into bytes; decode and validate it (7 rules); little-endian helpers |
| `hashtable.c` | In-memory index: separate chaining, FNV-1a 64-bit, doubles at load factor 0.75, owns copies of keys and values |
| `log.c` | Log header; append a record with flush and commit; roll back a partial write |
| `recovery.c` | Replay the log, stop at the first invalid record, truncate the tail, report |
| `compact.c` | Write a fresh log with only live keys into `cskv.log.tmp` |
| `snapshot.c` | Save (tmp + atomic rename) and load (full validation before use) snapshots |
| `kv.c` | Public API (`cskv.h`): ties the modules together, owns the lock and the log handle |
| `cli/` | `cskv.exe`: REPL, human and `--json` modes, input validation |
| `tools/cskv_corrupt.c` | Damages a log on purpose: truncate / flip / garbage |
| `gui/` | Tkinter control panel that runs `cskv.exe --json` |

The engine (`src/`) never prints; it reports errors only through `cskv_status`. Every
allocation has one owner, documented in the headers (for example, `cskv_get` returns a copy the
caller must `free`).

### 4.2 File formats

**Figure 2 — `docs/diagrams/file_formats.svg`.**

- **Log:** 8-byte header (`"CSKV"`, version 1, reserved), then records:
  `body_len(4) crc32(4) type(1) klen(4) vlen(4) key value`. CRC32 covers the body.
  Integers are written byte by byte in little-endian order — never by `fwrite` of a struct —
  so padding and CPU endianness never leak into the file.
- **Snapshot:** `"CSNP"`, version, reserved, `entry_count(8)`, entries `klen vlen key value`,
  then a CRC32 of everything before it.

### 4.3 Data flow

**Figure 3 — `docs/diagrams/write_and_compaction.svg`.**

- **SET / DEL:** validate → encode into one buffer → `fwrite` → `fflush` → `_commit` →
  update hash table → return OK. If the write fails, the file is cut back to its previous size
  and memory is not changed.
- **Compaction:** write `cskv.log.tmp` (header + one SET per live key), commit and close it,
  close the log, `MoveFileExA(tmp → cskv.log, REPLACE_EXISTING | WRITE_THROUGH)`, reopen.
  The original log is never modified before the atomic rename.

**Figure 4 — `docs/diagrams/recovery_flow.svg`.**

- **Recovery:** take the lock, delete a leftover `.tmp`, check the header, then read records
  from offset 8. At the first record that breaks a rule, record the reason and offset,
  truncate the file there (`_chsize_s`), commit, and stop.

### 4.4 Windows durability APIs

| Need | API | Why |
|---|---|---|
| Force data to disk | `fflush` + `_commit(_fileno(fp))` | `fflush` only reaches the OS cache; `_commit` is the Windows `fsync` |
| Atomic file replace | `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)` | readers see the old or the new file, never a mix |
| Cut a corrupted tail | `_chsize_s` | new appends start right after the last valid record |
| Single writer | `CreateFileA` with `FILE_SHARE_READ` and `FILE_FLAG_DELETE_ON_CLOSE` | a second writer gets a sharing violation; the file disappears even if the process is killed |
| Timing | `QueryPerformanceCounter` | recovery duration in stats |

---

## 5. Implementation details

### 5.1 Tools

C11 with MinGW-w64 gcc 16.2 (MSYS2 UCRT64), GNU Make, Python 3.11 + Tkinter + pytest 9,
Unity v2.6.0 (vendored, MIT licence). No third-party C libraries and no networking.

### 5.2 Key decisions and trade-offs

| Decision | Alternative | Why we chose it |
|---|---|---|
| Append-only log | update records in place | a crash can only damage the *last* record, which the checksum detects |
| Sync = always by default | sync = none | acknowledged data survives power loss; measured 1,664 SET/s vs 8,937 SET/s with `--sync none` |
| Tombstones for DEL | rewrite the file | deletes stay O(1) appends; compaction removes them later |
| Atomic rename for compaction/snapshot | write in place | a crash leaves either the complete old or the complete new file |
| Stop at the first bad record | skip bad records and continue | data after an unknown-length record cannot be trusted |
| Validate a snapshot completely before using it | stream it into the live table | a damaged backup can never corrupt the current state |
| One contiguous buffer per record | several `fwrite` calls | the record leaves the process in one write |
| Lock reported only if the lock file exists | always "locked" | permission problems are reported as I/O errors, not as "locked" |

### 5.3 Known limitations

See `KNOWN_ISSUES.md`. The most important: paths are limited to 259 characters (ANSI
Windows APIs); `RESTORE` reads the whole snapshot into memory; the GUI refreshes its table with
one `GET` per key, which is fine for demo-sized stores; values entered through the CLI/GUI
cannot contain line breaks or start with a space (the engine API has no such limit).

---

## 6. GUI design and operation

The GUI (`gui/app.py`) starts the real `build/cskv.exe --json` and talks to it through pipes:
one command per line, one JSON object back. A background thread only reads replies; all
callbacks run on the Tk thread through a queue polled every 30 ms, so the window never freezes.

| Area | Screenshot | Purpose |
|---|---|---|
| Top bar | all | data directory, engine status light, Start / Stop / Restart |
| Banner | `07_recovered_after_crash.png` | recovery result after each start: green clean, amber corruption, red engine error |
| Key / value tab | `02_data_with_dead_records.png` | Set / Get / Delete, live byte counters, red messages for invalid input |
| Maintenance tab | `03_after_compaction.png`, `04_snapshot_saved.png` | Compact (size before → after), Snapshot, Restore with confirmation |
| Crash lab tab | `06_crash_lab.png` | kill the engine, damage the log with `cskv_corrupt.exe`, restart |
| Data table | all | keys, value preview (80 chars), size, filter box |
| Activity log | all | every command sent and every reply, timestamped |
| Storage panel | all | live keys, log size, records, dead records, dead-space bar, last startup |

**Validation before sending:** empty key, key over 256 bytes, key with spaces or non-ASCII,
empty value, value over 1 MiB, line breaks or control characters in a value. RESTORE and
Simulate crash ask for confirmation.

**Error handling:** if `cskv.exe` is missing, fails to open the store (locked, bad format), or
exits unexpectedly, a red banner explains it and offers *Restart engine*; the GUI itself never
crashes (`09_validation_and_engine_error.png`).

**Screenshots of the demo scenario:** `docs/screenshots/01_…` to `09_…`.

---

## 7. Testing and results

### 7.1 Environment

Windows 11 Pro (build 22631), gcc 16.2.0 (MSYS2 UCRT64), Python 3.11.0, pytest 9.2.0.
Full output of the last run: `docs/test_results.txt`.

### 7.2 Plan

| Level | Tool | Count | Command |
|---|---|---|---|
| Unit | Unity | 57 tests | `make test` |
| Integration | pytest, real executables | 60 tests | `make test` |
| Memory | counting allocator | 57 tests | `make leakcheck` |
| Manual / GUI | demo scenario 9.4 | 7 steps | `make gui` |

### 7.3 Test cases (summary)

| Area | Normal use | Edge cases | Error conditions |
|---|---|---|---|
| CRC32 | known vector `0xCBF43926` | empty input = 0, incremental = one-shot | single bit change detected |
| Hash table | put / get / overwrite / remove | 12,000 keys with resizing, keys containing `\0`, empty value | remove missing key |
| Record | SET / DEL round trip | max key + max value | each of the 7 rules rejected on its own |
| Recovery | 1,000 records; 100,000 in < 2 s | header-only log | truncated header, truncated body, CRC flip in the middle, garbage, wrong magic, 3-byte file |
| Engine API | set / get / del / keys / stats | empty value, max sizes, missing dir created | oversize, NULL arguments, second open → LOCKED |
| Compaction | data identical, log shrinks | empty store | crash during compaction keeps the original |
| Snapshot | save / restore round trip, survives restart | empty store | flipped byte, wrong magic, truncated, missing file |
| Fuzzing | — | 150 random log tails, 100 random files, a flip at **every byte** of a log, 100 random + every-byte-flipped snapshots | never a crash; never bad data loaded |
| CLI | every command, case-insensitive, restart persistence | Arabic text and quotes in values, 1 MiB value, CRLF input, UTF-8 BOM | 12 invalid inputs, line > 1 MiB, bad arguments (exit 1), locked / bad format / file as dir (exit 2) |
| Crash | kill during 1,000 streamed writes: all 600 acknowledged keys survive | Ctrl+Z byte inside a value | truncate / flip / garbage via `cskv_corrupt.exe` |
| GUI client | start, send, ordered replies, clean stop | — | unexpected exit reported, locked store reported |

### 7.4 Expected vs actual

All 57 unit tests and 60 integration tests pass; expected values are written in the tests
themselves (for example the flipped-byte test expects exactly the keys before the damaged
record and the reason `"CRC mismatch at offset 50"`). Demo scenario 9.4 produced exactly the
results listed in section 3.4.

### 7.5 Memory checking

Dr. Memory 2.6.0 and the newest build 2.6.20434 (64-bit and 32-bit) crash on the test machine
even with a five-line program, so the cause is the tool/OS combination, not CrashSafeKV.
As replacement evidence, `make leakcheck` rebuilds the engine and all unit tests with counting
`malloc`/`calloc`/`realloc`/`free` wrappers; every test must end with as many live allocations
as it started with. Result: **0 leaks in 57 tests**. The check was validated by deliberately
removing one `free()` — the affected tests failed immediately. `make memcheck` still runs
Dr. Memory where it works. **[TODO: if a team member's machine runs Dr. Memory, add its
summary here.]**

### 7.6 Defects found and fixed

| Defect | Found by | Fix |
|---|---|---|
| A value containing byte `0x1A` (Ctrl+Z) ended the session (Windows text-mode stdin treats it as end of file) | edge-case and random-input tests | stdin is read in binary mode when it is a pipe |
| A data-directory path that is a file was reported as "locked by another process" | edge-case test | directory creation checks that existing parts are directories; LOCKED only when the lock file exists |
| GUI: side columns too wide, panels cut off at 150 % display scaling, window under the taskbar | screenshot review | tabbed left panel, window sized to the screen work area |
| Tests: a random `SNAPSHOT <name>` line left a snapshot file in the project folder | clean-copy build check | integration tests run the engine inside each test's temporary folder |
| A UTF-8 byte order mark at the start of piped input (PowerShell, Notepad) made the first command unknown | manual check before upload | BOM stripped from the first line; 2 tests added |
| GUI: "I/O error" displayed as "I/o error" | final review | first letter upper-cased only |
| GUI: a refresh re-selected a row and overwrote unsaved editor text | final review | rows load into the editor only on a user click or arrow key |

### 7.7 Remaining issues

See `KNOWN_ISSUES.md` (CLI/GUI input limits, path length, Dr. Memory on this machine).

---

## 8. Build and run instructions

(From `README.md`.)

1. Install MSYS2, then in the MSYS2 UCRT64 terminal: `pacman -S mingw-w64-ucrt-x86_64-gcc make`
2. Install pytest: `python -m pip install pytest`
3. In PowerShell, in the project folder:
   `$env:Path = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:Path`
4. `make` — builds `build/cskv.exe`, `build/cskv_corrupt.exe`, `build/libcskv.a`
5. `make test` — unit and integration tests
6. `make gui` — starts the GUI

---

## 9. Conclusion and future work

CrashSafeKV meets all functional requirements: acknowledged writes survive killing the
process, every kind of damage we could produce is detected and cut off, compaction and
snapshots are atomic, and the GUI shows all of it live on the real engine.

Future work: background compaction triggered by the dead-record ratio; multi-threaded access
with a reader/writer lock; range queries with an ordered index; group commit to raise
throughput in sync = always mode; a Linux port by adding a second `platform.c`.

---

## 10. References and appendices

- ThrowTheSwitch, *Unity Test Framework* v2.6.0 — https://github.com/ThrowTheSwitch/Unity (MIT)
- ISO/IEC 3309 / IEEE 802.3 CRC-32; reference value `CRC32("123456789") = 0xCBF43926`
- Fowler–Noll–Vo hash (FNV-1a) — http://www.isthe.com/chongo/tech/comp/fnv/
- Microsoft Learn: `MoveFileExA`, `CreateFileA`, `_commit`, `_chsize_s`, `_setmode`,
  `QueryPerformanceCounter`
- Appendix A — sample session: `samples/demo_commands.txt`
- Appendix B — full test output: `docs/test_results.txt`
- Appendix C — figures: `docs/diagrams/*.svg`; screenshots: `docs/screenshots/*.png`
