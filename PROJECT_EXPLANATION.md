# Project Explanation — CrashSafeKV

> **How to use this file.** It explains the whole project from zero, one step at a time, in
> plain language. You can read it yourself or paste it into any AI assistant and ask questions
> such as "explain section 6 more simply" or "what happens when the power goes off during a
> SET?". Every fact here matches the code in this repository.

---

## Table of contents

1. [The project in one paragraph](#1-the-project-in-one-paragraph)
2. [Name and identity](#2-name-and-identity)
3. [The core idea in plain words](#3-the-core-idea-in-plain-words)
4. [The ideas we merged](#4-the-ideas-we-merged)
5. [Goals](#5-goals)
6. [What the project does, exactly](#6-what-the-project-does-exactly)
7. [How it works, step by step](#7-how-it-works-step-by-step)
8. [File design (byte by byte)](#8-file-design-byte-by-byte)
9. [Architecture and modules](#9-architecture-and-modules)
10. [The important files](#10-the-important-files)
11. [Ways to use it](#11-ways-to-use-it)
12. [How we know it works (testing)](#12-how-we-know-it-works-testing)
13. [Build and run](#13-build-and-run)
14. [Design decisions and why](#14-design-decisions-and-why)
15. [Limits and known issues](#15-limits-and-known-issues)
16. [Glossary](#16-glossary)
17. [Explaining the project in one minute](#17-explaining-the-project-in-one-minute)

---

## 1. The project in one paragraph

**CrashSafeKV** is a small database that stores pairs of **key → value** (for example
`name → Ahmed`) in a file on a Windows computer. Its special skill is **surviving crashes**:
every change is added to the *end* of a log file together with a checksum, and the file is
never edited in the middle. When the program starts, it re-reads the log to rebuild its data.
If the computer crashed while the last change was being written, that last change is
incomplete; the checksum reveals this, the damaged part is cut off, and everything before it
is kept. It also shrinks its file when it grows (compaction), makes verified backups
(snapshots), and comes with a graphical control panel that can simulate a crash live.

---

## 2. Name and identity

| Item | Value |
|---|---|
| Name | **CrashSafeKV** ("crash-safe key-value store") |
| Full title | A Durable Append-Only Key-Value Storage Engine |
| Course | Systems Programming |
| Official catalog project | **#53 — Append-Only Key-Value Log** (Intermediate) — the base |
| Merged extensions | **#66** Checksummed WAL Appender, **#69** Log Compactor, **#58** Key-Value Snapshot Tool |
| Engine language | C11 (compiled with MinGW-w64 gcc) |
| GUI and integration tests | Python 3 (Tkinter, pytest) |
| Platform | Windows 10 / 11 only |
| Not included | networking, multiple threads, multiple processes writing at once |

---

## 3. The core idea in plain words

Imagine a shop owner's notebook where **erasing is forbidden**.

- Every change is written as a **new line at the end**: "Ahmed's age is 21". If the age
  changes, a new line is written: "Ahmed's age is 22". The old line stays.
- To know the current value, you **read the notebook from the first line to the last**;
  the last line about each name wins.
- To delete something, you write a line saying "Ahmed's age: deleted". This line is called a
  **tombstone**.

What if the lights go out while a line is being written? The line is **half written**.
To catch that, every line carries a **fingerprint** (a checksum called **CRC32**) calculated
from its content. When reading, if the fingerprint does not match the content, that line is
damaged. We **stop there**, keep every line before it, and **cut off** the damaged end so new
lines start from a clean point.

Two more problems and their fixes:

- The notebook keeps growing with old lines → **compaction**: copy only the latest value of
  each name into a new clean notebook, then swap the notebooks in one step.
- We want a backup → **snapshot**: a complete, fingerprinted copy of all current data in a
  separate file, which can be restored later.

That is the whole project. Everything else is making these steps correct and safe.

---

## 4. The ideas we merged

The course catalog offers small projects. We took one as the **base** and merged three
related ones into it, so they form a single complete system.

| # | Catalog project | What it adds | Where it lives |
|---|---|---|---|
| **53** | **Append-Only Key-Value Log** (base) | Every update is appended to a log; replay on startup rebuilds the state; detect corrupted records | `src/log.c`, `src/recovery.c`, `src/kv.c` |
| 66 | Checksummed WAL Appender | A length field and a CRC32 on every record; recovery stops at the first invalid record | `src/crc32.c`, `src/record.c`, `src/recovery.c` |
| 69 | Log Compactor | Rewrite the log keeping only the latest value per key; atomic replacement | `src/compact.c` |
| 58 | Key-Value Snapshot Tool | Save snapshots atomically, validate their checksum, restore safely | `src/snapshot.c` |

**How they fit together:** #53 gives the storage model (append + replay). #66 makes each record
verifiable, which is what makes crash recovery possible. #69 solves the main weakness of #53
(the file only grows). #58 adds backups that use the same checksum idea. The project is
registered as **#53**, and the report states that #66, #69 and #58 were integrated as
extensions.

> "WAL" means **write-ahead log**: write the change to the log first, and only then treat it
> as done. That is exactly what CrashSafeKV does.

---

## 5. Goals

1. **Never lose acknowledged data.** If `SET` answered OK, the data survives a crash or a
   power cut.
2. **Detect corruption** with CRC32 and **never load bad data**.
3. **Recover automatically** to the last consistent state on startup.
4. **Stay small over time** through compaction.
5. **Support safe backups** through atomic, verified snapshots.
6. **Be demonstrable**: a GUI that can simulate a crash and show the recovery live.

Why this is a *systems* project: it works with files at byte level, binary formats,
forcing data from memory to the physical disk, atomic file replacement, manual memory
management in C, hash tables, error handling, and one process controlling another (the GUI
starts and talks to the engine).

---

## 6. What the project does, exactly

### 6.1 Operations

| Command | What it does | Example |
|---|---|---|
| `SET <key> <value>` | store a value or replace the old one | `SET name Ahmed Ali` → `OK` |
| `GET <key>` | read a value | `GET name` → `"Ahmed Ali"` |
| `DEL <key>` | delete a key (writes a tombstone) | `DEL name` → `OK` |
| `KEYS` | list all keys, sorted | `1) "age"  2) "name"` |
| `STATS` | live keys, log size, record count, dead records, last recovery | |
| `COMPACT` | rewrite the log with live keys only | `OK (log 221 -> 128 bytes)` |
| `SNAPSHOT <file>` | save a full, checksummed backup | `SNAPSHOT backup.snap` |
| `RESTORE <file>` | replace all data with a backup (only if the backup is valid) | `RESTORE backup.snap` |
| `HELP`, `EXIT` | help, quit cleanly | |

Deleting a key that does not exist answers "not found" and writes nothing.

### 6.2 Limits

| Thing | Limit |
|---|---|
| Key length | 1 to 256 bytes |
| Value length | 0 bytes to 1 MiB (1,048,576 bytes) |
| Keys typed in the CLI / GUI | printable ASCII, no spaces |
| Values typed in the CLI / GUI | the rest of the line (spaces allowed) |

### 6.3 The three programs

| Program | Purpose |
|---|---|
| `build/cskv.exe` | the database with a command prompt (`cskv> `) and a machine mode (`--json`) |
| `build/cskv_corrupt.exe` | damages a log file on purpose, to test recovery |
| `gui/app.py` | a dark-themed control panel that starts the real `cskv.exe` and drives it |

---

## 7. How it works, step by step

### 7.1 Writing a value (`SET name Ahmed`)

1. Check the input: key length 1–256, value up to 1 MiB.
2. Build the record (section 8) in one block of memory.
3. Write the whole block to the end of `cskv.log` (`fwrite`).
4. `fflush`: hand the bytes to Windows (now a killed process cannot lose them).
5. `_commit`: force Windows to put them on the physical disk (now a power cut cannot lose
   them).
6. Only now update the in-memory **hash table**.
7. Answer `OK`.

If any of steps 3–5 fails, the file is cut back to its old size, the hash table is not
touched, and an error is returned. This order — **disk first, memory second, answer last** —
is the heart of the "never lose acknowledged data" goal.

### 7.2 Starting up (recovery)

1. Create the data folder if it is missing.
2. Take the **lock file** `cskv.lock`. If another program holds it, stop with "locked".
3. If a leftover `cskv.log.tmp` exists, delete it (a compaction crashed; the real log is still
   valid).
4. If `cskv.log` does not exist or is empty, create it with a header and finish.
5. Check the 8-byte header (`"CSKV"`, version 1). Wrong → "bad format".
6. Starting right after the header, read records one by one:
   - **valid** → apply it (SET: put into the table; DEL: remove from the table), continue;
   - **invalid** → remember why and where, **cut the file** at that point, commit, stop;
   - **no bytes left** → clean end.
7. Report what happened, for example:
   `Recovered 1532 records in 12.4 ms. Corruption detected: CRC mismatch at offset 48211 - discarded 37 bytes.`

A record is **invalid** if any of these 7 rules fails:

1. fewer than 8 bytes left (truncated header);
2. the length field is impossible (too small or too large);
3. fewer bytes left than the length says (truncated body);
4. the CRC32 does not match the content;
5. the type is not 1 (SET) or 2 (DEL);
6. key/value lengths are inconsistent (empty key, too long, or they do not add up);
7. a DEL record carries a value.

### 7.3 Compaction

1. Write a new file `cskv.log.tmp`: header + one SET record per live key.
2. Flush, commit and close it.
3. Close the current log.
4. **Atomically** rename the tmp file over `cskv.log` (`MoveFileExA` with
   `REPLACE_EXISTING | WRITE_THROUGH`).
5. Reopen the log for appending. Dead records are now 0.

A crash before step 4 leaves the old log untouched (the tmp file is deleted at the next
start). A crash after step 4 leaves the complete new log. There is never a mix of both.

### 7.4 Snapshot and restore

- **Snapshot:** write everything to `<file>.tmp` with a CRC32 of the whole file at the end,
  commit, then atomically rename to `<file>`.
- **Restore:** read the entire snapshot and check the magic, the version, the CRC and every
  length **before** touching anything. Only if all of it is valid: build a new table, write it
  as a new log (same steps as compaction), then swap. A damaged backup is rejected and the
  current data stays exactly as it was.

### 7.5 One user at a time

`cskv.lock` is opened with Windows sharing rules that let only one writer hold it, and it is
deleted automatically when the program closes or is killed. A second `cskv.exe` on the same
folder gets "store is locked by another process".

---

## 8. File design (byte by byte)

All numbers are stored **little-endian** (lowest byte first) and written byte by byte, never
by dumping a C struct, so the file layout does not depend on the compiler or the CPU.

### 8.1 The log file `cskv.log`

**Header (8 bytes, once at the start):**

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | ASCII `CSKV` |
| 4 | 2 | version | 1 |
| 6 | 2 | reserved | 0 |

**Record (repeated):**

| Field | Size | Meaning |
|---|---|---|
| `body_len` | 4 | number of bytes after the CRC field = `9 + klen + vlen` |
| `crc32` | 4 | CRC32 of the body (from `type` to the end of `value`) |
| `type` | 1 | 1 = SET, 2 = DEL |
| `klen` | 4 | key length |
| `vlen` | 4 | value length (0 for DEL) |
| `key` | klen | key bytes |
| `value` | vlen | value bytes |

Total record size = `8 + body_len`.

**Real example.** These are the exact bytes `cskv.exe` writes after the header for
`SET name Ahmed` (verified on the real program):

```
12 00 00 00   body_len = 0x12 = 18  (9 + 4 + 5)
6A 68 8D 55   crc32    = 0x558D686A
01            type     = SET
04 00 00 00   klen     = 4
05 00 00 00   vlen     = 5
6E 61 6D 65   "name"
41 68 6D 65 64  "Ahmed"
```

If a crash cut this record after, say, 20 bytes, rule 3 (truncated body) catches it. If one
bit inside "Ahmed" flipped, rule 4 (CRC mismatch) catches it.

### 8.2 The snapshot file `*.snap`

| Field | Size | Meaning |
|---|---|---|
| magic | 4 | ASCII `CSNP` |
| version | 2 | 1 |
| reserved | 2 | 0 |
| entry_count | 8 | number of entries |
| entries | … | each: `klen (4)`, `vlen (4)`, key, value |
| file_crc | 4 | CRC32 of **every byte before it** |

### 8.3 The data folder

```
data/
├── cskv.log        the append-only log
├── cskv.lock       exists only while a program has the store open
└── cskv.log.tmp    exists only during compaction / restore
```

Diagrams: `docs/diagrams/file_formats.svg`.

---

## 9. Architecture and modules

```
 GUI (Python)  ── one command per line / one JSON line back ──▶  cskv.exe (cli/)
                                                                     │  cskv.h API
                                                                     ▼
                                   kv.c  (engine facade: owns the lock, the log, the table)
                ┌──────────────┬──────────────────────┬──────────────┬──────────────┐
                hashtable.c    log.c + recovery.c     compact.c      snapshot.c
                (memory)       (append / replay)      (shrink)       (backup)
                └──────────────┴────────── record.c + crc32.c ───────┴──────────────┘
                                         (binary format, checksums)
                                                   │
                                     platform.c  (all Windows calls)
                                                   │
                                  data/cskv.log, data/cskv.lock, *.snap
```

Layers, from bottom to top:

1. **platform** — the only file that knows it runs on Windows.
2. **crc32 + record** — turn a change into bytes and check bytes for damage.
3. **hashtable** — fast in-memory lookup (average O(1) `GET`).
4. **log, recovery, compact, snapshot** — the four storage operations.
5. **kv** — the public API (`include/cskv.h`) that ties everything together.
6. **cli** — the `cskv.exe` program.
7. **gui** — the Python control panel that runs `cskv.exe`.

Rules the engine follows: it **never prints** (only the CLI and tools print); it has **no
global mutable state** (everything hangs off one `cskv` object); every allocation has **one
owner**; every function that can fail returns a status code.

Full diagram: `docs/diagrams/architecture.svg`.

---

## 10. The important files

### Engine (C)

| File | What it contains |
|---|---|
| `include/cskv.h` | the public API: `cskv_open`, `cskv_set`, `cskv_get`, `cskv_del`, `cskv_keys`, `cskv_compact`, `cskv_snapshot`, `cskv_restore`, `cskv_get_stats`, error codes, limits |
| `src/kv.c` | implements the API; opening, locking, the durability order, compaction/restore swap |
| `src/log.c` | log header; append one record with flush + commit; roll back a partial write |
| `src/recovery.c` | replay the log, apply the 7 rules, cut the damaged tail |
| `src/record.c` | encode/decode one record; little-endian helpers |
| `src/crc32.c` | the CRC32 checksum (table-driven) |
| `src/hashtable.c` | the in-memory map (FNV-1a hash, chaining, grows at 75 % full) |
| `src/compact.c` | write a fresh log of live keys into `cskv.log.tmp` |
| `src/snapshot.c` | save and load snapshot files |
| `src/platform.c` | Windows calls: `_commit`, `_chsize_s`, `MoveFileExA`, lock file, folders, timer |
| `src/errors.c` | human-readable error messages |

### Programs

| File | What it contains |
|---|---|
| `cli/main.c` | arguments, opening the store, the read loop, exit codes |
| `cli/commands.c` | parsing and running each command, input checks |
| `cli/output.c` | all answers, in human form or as JSON |
| `tools/cskv_corrupt.c` | `truncate N`, `flip OFFSET`, `garbage N` |

### GUI (Python)

| File | What it contains |
|---|---|
| `gui/app.py` | the window, layout and all button actions |
| `gui/engine_client.py` | starts `cskv.exe --json`, sends commands, reads replies on a background thread |
| `gui/panels.py` | the panels: key/value editor, data table, storage stats, maintenance, crash lab, activity log |
| `gui/widgets.py` | custom flat buttons, inputs, tabs, banner |
| `gui/theme.py` | colours and fonts |
| `gui/rules.py` | input checks (same rules as the CLI) |

### Tests, build and documents

| File / folder | What it contains |
|---|---|
| `tests/test_*.c` | 57 unit tests (Unity framework, vendored in `tests/unity/`) |
| `tests/memtrack.*` | the allocation counter used by `make leakcheck` |
| `tests/integration/` | 60 pytest tests that run the real programs |
| `Makefile` | `make`, `make test`, `make leakcheck`, `make gui`, `make memcheck`, `make clean` |
| `CLAUDE.md` | the full original specification |
| `README.md` | quick start and usage |
| `KNOWN_ISSUES.md` | limits, and every defect found and fixed |
| `docs/REPORT.md` | the course report draft |
| `docs/diagrams/` | architecture, file formats, recovery flow, write/compaction flow |
| `docs/screenshots/` | the demo scenario, step by step |
| `docs/test_results.txt` | the last full test run |

---

## 11. Ways to use it

### 11.1 Command line (human mode)

```
> build\cskv.exe
Recovered 0 records in 0.0 ms.
cskv> SET name Ahmed Ali
OK
cskv> GET name
"Ahmed Ali"
cskv> GET nothing
(nil)
```

Options: `--data-dir DIR` (default `.\data`), `--sync always|none` (default `always`),
`--json`, `--help`. Exit codes: 0 normal, 1 bad arguments, 2 the store could not be opened.

### 11.2 JSON mode (for programs)

`cskv.exe --json` prints exactly one JSON object per line. The first line is the recovery
report:

```
{"event":"recovery","records":5,"bytes_valid":154,"bytes_discarded":21,"corruption":true,"reason":"truncated body at offset 154","ms":1.2}
```

Then one answer per command:

```
SET a 1     →  {"ok":true}
GET a       →  {"ok":true,"value":"1"}
GET x       →  {"ok":false,"error":"NOT_FOUND","message":"key not found"}
KEYS        →  {"ok":true,"keys":["a"]}
```

### 11.3 The GUI

`make gui` (or `python gui/app.py`). Top bar: data folder, engine status, Start / Stop /
Restart. Left: tabs **Key / value**, **Maintenance** (Compact, Snapshot, Restore) and
**Crash lab**. Centre: the data table and a log of every command and reply. Right: storage
numbers and the last recovery. A coloured banner shows the recovery result after each start:
green when clean, amber when corruption was found and repaired, red when the engine failed.

**Demo scenario:** add 5 keys, overwrite 2, delete 1 → compact → snapshot → add 2 keys →
*Simulate crash* (kill the engine and cut 5 bytes) → the banner turns amber, only the last
key is lost → restore the snapshot → the data matches the snapshot. Screenshots of each step
are in `docs/screenshots/`.

### 11.4 From C code

```c
cskv *db;
if (cskv_open("data", CSKV_SYNC_ALWAYS, &db) != CSKV_OK) { /* handle error */ }
cskv_set(db, "name", 4, "Ahmed", 5);
void *val; size_t len;
if (cskv_get(db, "name", 4, &val, &len) == CSKV_OK) { /* use val */ free(val); }
cskv_close(db);
```

---

## 12. How we know it works (testing)

| Suite | Count | What it proves |
|---|---|---|
| Unit tests (C, Unity) | 57 | every module: CRC vectors, hash table with 12,000 keys, each of the 7 record rules, recovery from every kind of damage, compaction, snapshots, the lock |
| Fuzz tests (part of the 57) | 4 | hundreds of random and damaged files; a flipped byte at **every position** of a log — never a crash, never bad data loaded |
| Integration tests (pytest) | 60 | every command through the real `cskv.exe`; killing the engine during 1,000 writes keeps all 600 acknowledged keys; all corruption modes; exit codes; random input lines; the GUI's engine client |
| Leak check (`make leakcheck`) | 57 | every test frees everything it allocated (0 leaks) |
| Performance | — | recovering 100,000 records takes about **68 ms** (requirement: under 2 s) |

Defects found by these tests and fixed are listed in `KNOWN_ISSUES.md`, for example: a byte
`0x1A` (Ctrl+Z) inside a value used to end the session; an invisible UTF-8 BOM at the start of
piped input used to make the first command unknown.

---

## 13. Build and run

1. Install **MSYS2**, then in its "UCRT64" terminal:
   `pacman -S mingw-w64-ucrt-x86_64-gcc make`
2. Install **Python 3.10+**, then `python -m pip install pytest`
3. In PowerShell, in the project folder:
   `$env:Path = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:Path`
4. `make` — builds everything into `build/`
5. `make test` — runs all tests
6. `make gui` — opens the control panel

---

## 14. Design decisions and why

| Decision | Why |
|---|---|
| Append only, never edit in place | a crash can damage only the last record, and the checksum catches it |
| Disk first, then memory, then OK | an OK always means the data is on disk |
| Stop at the first bad record | after an unreadable length, nothing later can be trusted |
| Cut the damaged tail | new records start from a clean point instead of after garbage |
| Tombstones for delete | delete is a fast append; compaction cleans up later |
| Write to `.tmp`, then atomic rename | a crash leaves the old or the new file, never half of each |
| Validate a backup completely before using it | a damaged backup can never damage current data |
| `sync = always` by default (`none` optional) | safety first; `none` is about 5× faster but may lose the latest writes on power loss |
| All Windows code in one file | the rest of the engine is plain C and easier to read and port |
| GUI talks to the real program through JSON lines | what the GUI shows is exactly what the engine does |

---

## 15. Limits and known issues

- Windows only; no networking, no multiple threads, no multi-key transactions, no
  encryption, no range queries.
- Paths are limited to 259 characters.
- From the CLI/GUI, values cannot contain line breaks or start with a space, and empty values
  cannot be stored (the C API has no such limit).
- `RESTORE` reads the whole snapshot into memory.
- Dr. Memory (the memory checker named in the spec) crashes on the development machine even
  on a tiny program; `make leakcheck` is used instead and reports 0 leaks.

Full list: `KNOWN_ISSUES.md`.

---

## 16. Glossary

| Term | Meaning |
|---|---|
| Key-value store | a database that maps a name (key) to data (value) |
| Append-only log | a file where data is only ever added at the end |
| Record | one change in the log (one SET or one DEL) |
| Tombstone | a DEL record: "this key is deleted" |
| Replay / recovery | reading the log from the start to rebuild the data after a restart |
| CRC32 | a 32-bit checksum; changes when even one bit of the content changes |
| Durability | data that was confirmed stays saved, even after a crash |
| `fflush` / `_commit` | push data from the program to Windows / from Windows to the disk |
| Atomic rename | replacing a file in one step: others see the old or the new one, never a mix |
| Compaction | rewriting the log with only the latest value of each key |
| Snapshot | a complete, checksummed backup file of all data |
| Dead record | a record that no longer matters (overwritten, deleted, or a tombstone) |
| Hash table | an in-memory structure that finds a key in about constant time |
| Lock file | a file that proves one program is using the store |
| Little-endian | storing numbers lowest byte first |
| BOM | an invisible marker some editors put at the start of a text file |

---

## 17. Explaining the project in one minute

"CrashSafeKV is a crash-safe key-value database in C for Windows. Every change is appended to
a log with a CRC32 checksum, and we only answer OK after the change is physically on disk.
On startup we replay the log; if the last record was half written because of a crash, the
checksum or length check catches it, we cut it off and keep everything before it. Compaction
rewrites the log with only live keys and swaps it in atomically, and snapshots give verified
backups that are checked completely before restoring. A Python GUI drives the real engine and
can kill it, damage the file, and show the recovery live. It is catalog project #53 with
#66, #69 and #58 merged in, and it is covered by 57 unit tests, 60 integration tests and a
leak check."
