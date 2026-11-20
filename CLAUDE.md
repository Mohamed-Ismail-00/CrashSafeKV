# CLAUDE.md — CrashSafeKV Project Specification

> This file is the single source of truth for the project. Read it fully before writing any code.
> Every design decision below is intentional. If something is ambiguous, follow the **Constraints** section first, then ask the team before deviating.

---

## 0. Quick Facts

| Item | Value |
|---|---|
| Project name | **CrashSafeKV** — A Durable Append-Only Key-Value Storage Engine |
| Official catalog project | **#53 — Append-Only Key-Value Log** (Intermediate) |
| Extensions merged in | #66 Checksummed WAL Appender, #69 Log Compactor, #58 Key-Value Snapshot Tool |
| Course | Systems Programming |
| Language (engine) | **C11** |
| Language (GUI + integration tests) | **Python 3.10+** (Tkinter, standard library only + pytest) |
| Target OS | **Windows 10/11 only** |
| Compiler | **MinGW-w64 gcc** (primary). MSVC should also compile if possible. |
| Team size | 8–10 members |
| Networking | **NONE.** No sockets, no servers, no HTTP. |
| Linux APIs | **NONE.** No `fork`, `/proc`, `fsync`, `pthread`, `epoll`, `unistd.h`. |

---

## 1. Core Idea and Main Goal

### 1.1 The idea in plain words
CrashSafeKV is a small database that stores **key → value** pairs (e.g. `name → Ahmed`, `age → 21`) in a file on disk.
**Every change is appended to the end of a log file** together with a checksum. The file is never edited in the middle.

When the program starts, it **replays the log from the beginning** to rebuild the in-memory state.
If the program crashed while writing (power loss, kill, bug), the last record may be half-written. The checksum detects this, the engine **stops at the first corrupted record**, keeps everything valid before it, and reports what happened.

### 1.2 Main goal
Build a storage engine that:
1. **Never loses acknowledged data.** If `SET` returned OK, the data survives a crash.
2. **Detects corruption** using CRC32 and never loads bad data.
3. **Recovers automatically** to the last consistent state on startup.
4. **Stays small over time** through compaction.
5. **Supports safe backups** through atomic snapshots.
6. **Is demonstrable** through a GUI that can simulate a crash and show recovery live.

### 1.3 Why this is a systems project
It exercises: file I/O at byte level, binary formats, durability (flushing OS buffers to disk), atomic file replacement, memory ownership in C, hash tables, error handling, and process control (GUI spawning the engine).

---

## 2. Merged Catalog Projects

| # | Catalog name | What we take from it | Where it lives in our code |
|---|---|---|---|
| **53** | **Append-Only Key-Value Log** | **Base project.** Every update appended to a log; replay on startup to reconstruct state; detect corrupted records. | `src/log.c`, `src/recovery.c`, `src/kv.c` |
| 66 | Checksummed WAL Appender | Length field + CRC32 per record; recovery stops at the first invalid record. | `src/crc32.c`, `src/record.c`, `src/recovery.c` |
| 69 | Log Compactor | Rewrite the log keeping only the latest value per key; handle malformed records; preserve correctness. | `src/compact.c` |
| 58 | Key-Value Snapshot Tool | Write snapshots atomically, validate checksums, restore state safely. | `src/snapshot.c` |

**Registration:** the project is registered as **#53**. The report must state that #66, #69 and #58 were integrated as extensions.

---

## 3. Final Deliverables (what we hand in)

All three must describe and demonstrate **the same system**. Claims in the report must be verifiable from the code and GUI.

### 3.1 Deliverable 1 — Codebase
- `cskv.exe` — the engine with an interactive CLI and a machine-readable `--json` mode.
- `cskv_corrupt.exe` — a small tool that damages a log file on purpose (for tests and the crash demo).
- `libcskv` — engine compiled as a static library (`libcskv.a`) used by the CLI and the unit tests.
- Unit tests (C, Unity framework) and integration tests (Python, pytest).
- `Makefile`, `README.md`, sample data, `KNOWN_ISSUES.md`.

### 3.2 Deliverable 2 — GUI
- `gui/app.py` — Tkinter control panel that **launches and talks to the real `cskv.exe`** (not a mockup).

### 3.3 Deliverable 3 — Report
See section 11 for the required outline.

---

## 4. Work Plan (what we do, in order)

| Phase | Week | Tasks | Done when |
|---|---|---|---|
| 0. Setup | 1 | Repo, folder structure, Makefile, `platform.h`, agree on `cskv.h` API and file format (this document) | `make` builds an empty `cskv.exe` with zero warnings |
| 1. Core | 1 | `crc32.c`, `hashtable.c`, `record.c` (encode/decode) | Unit tests for these pass |
| 2. Durability | 2 | `log.c` (append + flush + commit), `recovery.c` (replay + corruption detection + truncate) | Kill-and-restart test keeps all acknowledged data |
| 3. Maintenance | 2 | `compact.c`, `snapshot.c` (save/restore) | Compaction and snapshot tests pass, including crash-during-compaction |
| 4. Interface | 2–3 | `cli.c` REPL + `--json` mode, `cskv_corrupt.c` | Integration tests can drive the CLI |
| 5. GUI | 3 | `gui/app.py` with all screens in section 9 | Full demo scenario (section 9.4) works end to end |
| 6. Testing | 3 | Full test suite, Dr. Memory run, edge/error cases | All tests green, zero leaks reported |
| 7. Docs | 4 | README, report, diagrams, screenshots, clean-machine build check | Fresh clone builds and runs using only README steps |

---

## 5. Requirements

### 5.1 Course rules (mandatory)
1. Team of 8–10 with clearly assigned responsibilities.
2. **No deadline extensions.**
3. Functional testing covering **normal use, edge cases, and error conditions**, documented in the report and reproducible from the codebase.
4. All three deliverables (report, code, GUI) consistent with each other.
5. GUI must exercise the real implementation. A static screen is not acceptable.
6. Must build and launch from the README steps in a clean environment.
7. Meaningful error handling and input validation with clear messages.
8. **No secrets, credentials, or machine-specific absolute paths** in the repo.
9. State incomplete features and known defects explicitly (`KNOWN_ISSUES.md` + report).
10. Code comments for non-obvious behaviour, interfaces and algorithms.

### 5.2 Functional requirements
| ID | Requirement |
|---|---|
| FR-1 | `SET key value` stores or overwrites a value. |
| FR-2 | `GET key` returns the value or a clear "not found". |
| FR-3 | `DEL key` deletes a key (appends a tombstone record). Deleting a missing key returns "not found" and writes nothing. |
| FR-4 | `KEYS` lists all live keys (sorted alphabetically for deterministic output). |
| FR-5 | Every successful `SET`/`DEL` is appended to the log and made durable **before** OK is returned (when sync mode = `always`). |
| FR-6 | On startup the log is replayed and the in-memory state rebuilt. |
| FR-7 | A record with a bad CRC, impossible length, unknown type, or truncated body is detected; replay stops there; all earlier records are kept. |
| FR-8 | After corruption is detected, the corrupted tail is truncated so new appends start from a clean point. The number of discarded bytes is reported. |
| FR-9 | `COMPACT` rewrites the log with only live keys; old file replaced atomically. |
| FR-10 | `SNAPSHOT <file>` writes a full, checksummed, atomic snapshot. |
| FR-11 | `RESTORE <file>` validates the snapshot fully before replacing any state; on failure, current state is untouched. |
| FR-12 | `STATS` reports key count, log size, record count, dead records, last recovery result. |
| FR-13 | Invalid input (empty key, key too long, key with whitespace via CLI, value too large, unknown command) is rejected with a clear message and non-zero error code. |
| FR-14 | Only one process may open a given data directory at a time (lock file). |

### 5.3 Non-functional requirements
| ID | Requirement |
|---|---|
| NFR-1 | Compiles with `-std=c11 -Wall -Wextra -Wpedantic -Werror` with zero warnings. |
| NFR-2 | Zero memory leaks under Dr. Memory for the full test suite. |
| NFR-3 | `GET` is O(1) average (hash table). |
| NFR-4 | Recovery of a 100,000-record log completes in under 2 seconds on a normal laptop. |
| NFR-5 | No crash (segfault/abort) on any input, including random garbage files. |
| NFR-6 | All Windows-specific calls isolated in `src/platform.c` / `include/platform.h`. |

### 5.4 Limits
| Limit | Value | Constant |
|---|---|---|
| Key length | 1 – 256 bytes | `CSKV_MAX_KEY_LEN` |
| Value length | 0 – 1,048,576 bytes (1 MiB) | `CSKV_MAX_VALUE_LEN` |
| CLI keys | printable ASCII, no spaces/tabs | enforced in `cli.c` only |
| CLI values | rest of the line after the key (may contain spaces) | — |

The engine API itself accepts arbitrary bytes in keys/values; the CLI is more restrictive.

### 5.5 Out of scope (state this in the report)
Networking, multi-process concurrent access, multi-threading, transactions across multiple keys, encryption, range queries, Linux/macOS support.

---

## 6. Tools

| Purpose | Tool | Notes |
|---|---|---|
| Language | C11 | No compiler extensions in portable code |
| Compiler | MinGW-w64 gcc via **MSYS2** | `pacman -S mingw-w64-ucrt-x86_64-gcc make` |
| Build | GNU Make (`mingw32-make` or MSYS2 `make`) | Targets in section 8.6 |
| File I/O | `fopen`/`fread`/`fwrite`/`fseek`/`ftell` (use `_fseeki64`/`_ftelli64` for large files) | |
| Durability | `fflush()` then `_commit(_fileno(fp))` | Windows equivalent of `fsync` |
| Atomic replace | `MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING \| MOVEFILE_WRITE_THROUGH)` | |
| Truncate | `_chsize_s(_fileno(fp), new_size)` | |
| Lock file | `CreateFileA(..., FILE_SHARE_READ, ..., CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, ...)` | Fails if another instance holds it |
| Timing | `QueryPerformanceCounter` | For stats/benchmarks |
| Checksum | CRC32 (IEEE 802.3, poly `0xEDB88320`), table-driven, written by us | |
| Unit tests | **Unity** (ThrowTheSwitch) — vendored into `tests/unity/` | MIT licence, cite in report |
| Integration tests | Python **pytest** driving `cskv.exe --json` | |
| GUI | Python **Tkinter** + `subprocess` | Standard library only |
| Memory checking | **Dr. Memory** | `drmemory -- build\cskv_tests.exe` |
| Version control | Git + GitHub | Feature branches + pull requests |
| Diagrams | draw.io | Export PNG into `docs/diagrams/` |
| Report | Word / Google Docs → PDF | |

---

## 7. Repository Layout

```
crashsafekv/
├── CLAUDE.md                 # this file
├── README.md
├── KNOWN_ISSUES.md
├── Makefile
├── include/
│   ├── cskv.h                # PUBLIC engine API (section 8.3)
│   ├── platform.h            # Windows wrappers
│   ├── crc32.h
│   ├── hashtable.h
│   └── record.h
├── src/
│   ├── platform.c
│   ├── crc32.c
│   ├── hashtable.c
│   ├── record.c              # encode/decode one record
│   ├── log.c                 # open/append/flush log
│   ├── recovery.c            # replay + corruption detection
│   ├── compact.c
│   ├── snapshot.c
│   ├── kv.c                  # ties everything together, implements cskv.h
│   └── errors.c              # cskv_strerror()
├── cli/
│   └── main.c                # REPL + --json mode
├── tools/
│   └── cskv_corrupt.c        # damage a log file on purpose
├── tests/
│   ├── unity/                # vendored Unity framework
│   ├── test_crc32.c
│   ├── test_hashtable.c
│   ├── test_record.c
│   ├── test_recovery.c
│   ├── test_compact.c
│   ├── test_snapshot.c
│   ├── test_main.c
│   └── integration/
│       ├── conftest.py
│       └── test_cli.py
├── gui/
│   ├── app.py
│   └── engine_client.py      # subprocess wrapper around cskv.exe --json
├── samples/
│   └── demo_commands.txt
├── docs/
│   ├── diagrams/
│   └── screenshots/
└── build/                    # generated, git-ignored
```

---

## 8. Technical Design

### 8.1 Architecture
```
 ┌──────────────┐   stdin/stdout (JSON lines)   ┌──────────────────────────┐
 │ GUI (Python) │ ─────────────────────────────▶│ cskv.exe  (cli/main.c)   │
 └──────────────┘                                └────────────┬─────────────┘
                                                              │ cskv.h API
                     ┌────────────────────────────────────────▼────────────┐
                     │ kv.c  (engine facade)                               │
                     ├───────────┬───────────┬────────────┬────────────────┤
                     │ hashtable │ log.c     │ compact.c  │ snapshot.c     │
                     │ (RAM)     │ recovery.c│            │                │
                     ├───────────┴─────┬─────┴────────────┴────────────────┤
                     │ record.c + crc32.c (binary format)                  │
                     ├─────────────────▼───────────────────────────────────┤
                     │ platform.c (Windows file APIs)                      │
                     └─────────────────────────────────────────────────────┘
                                  data/cskv.log, data/cskv.lock
```

### 8.2 Data directory
```
<data-dir>/
├── cskv.log            # the append-only log
├── cskv.log.tmp        # exists only during compaction/restore; deleted on startup if found
└── cskv.lock           # held while a process has the store open
```
Default data dir: `./data` (relative to current working directory). Override with `--data-dir`.

### 8.3 Public API (`include/cskv.h`)
```c
#ifndef CSKV_H
#define CSKV_H
#include <stddef.h>
#include <stdint.h>

#define CSKV_MAX_KEY_LEN    256u
#define CSKV_MAX_VALUE_LEN  (1024u * 1024u)

typedef enum {
    CSKV_OK = 0,
    CSKV_ERR_NOT_FOUND,
    CSKV_ERR_INVALID_ARG,     /* NULL ptr, empty key, too long, etc. */
    CSKV_ERR_NO_MEMORY,
    CSKV_ERR_IO,              /* fopen/fwrite/fread/commit failed */
    CSKV_ERR_CORRUPT,         /* bad header / bad snapshot checksum */
    CSKV_ERR_LOCKED,          /* another process holds the store */
    CSKV_ERR_BAD_FORMAT       /* wrong magic / unsupported version */
} cskv_status;

typedef enum { CSKV_SYNC_ALWAYS = 0, CSKV_SYNC_NONE = 1 } cskv_sync_mode;

typedef struct {
    uint64_t records_replayed;
    uint64_t bytes_valid;
    uint64_t bytes_discarded;     /* corrupted tail that was truncated */
    int      corruption_found;    /* 0/1 */
    char     reason[128];         /* e.g. "CRC mismatch at offset 4096" */
    double   duration_ms;
} cskv_recovery_info;

typedef struct {
    uint64_t live_keys;
    uint64_t log_bytes;
    uint64_t log_records;         /* total records in current log */
    uint64_t dead_records;        /* overwritten or deleted entries */
    cskv_recovery_info last_recovery;
} cskv_stats;

typedef struct cskv cskv;         /* opaque */

cskv_status cskv_open(const char *data_dir, cskv_sync_mode mode, cskv **out);
void        cskv_close(cskv *db);

cskv_status cskv_set(cskv *db, const void *key, size_t klen,
                     const void *val, size_t vlen);
/* *out_val is malloc'd by the engine; caller must free(). */
cskv_status cskv_get(cskv *db, const void *key, size_t klen,
                     void **out_val, size_t *out_vlen);
cskv_status cskv_del(cskv *db, const void *key, size_t klen);

/* Calls cb for each live key in sorted order. Stop early if cb returns non-zero. */
typedef int (*cskv_key_cb)(const void *key, size_t klen, void *user);
cskv_status cskv_keys(cskv *db, cskv_key_cb cb, void *user);

cskv_status cskv_compact(cskv *db);
cskv_status cskv_snapshot(cskv *db, const char *path);
cskv_status cskv_restore(cskv *db, const char *path);
cskv_status cskv_get_stats(cskv *db, cskv_stats *out);

const char *cskv_strerror(cskv_status s);
#endif
```

### 8.4 Log file format (`cskv.log`)
All integers are **little-endian**, written byte-by-byte by helper functions (never `fwrite` a struct — padding and endianness would leak into the file).

**File header (8 bytes):**
| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | ASCII `"CSKV"` |
| 4 | 2 | version | `1` |
| 6 | 2 | reserved | `0` |

**Record (repeated):**
| Field | Size | Description |
|---|---|---|
| `body_len` | 4 | Number of bytes after the CRC field = `1 + 4 + 4 + klen + vlen` |
| `crc32` | 4 | CRC32 over the **body** (type through value) |
| `type` | 1 | `1` = SET, `2` = DEL |
| `klen` | 4 | key length |
| `vlen` | 4 | value length (must be `0` for DEL) |
| `key` | klen | key bytes |
| `value` | vlen | value bytes |

Total record size = `8 + body_len`.

**Validation rules when reading a record** (any failure = corruption, stop replay):
1. Fewer than 8 bytes remain → truncated header.
2. `body_len < 9` or `body_len > 9 + CSKV_MAX_KEY_LEN + CSKV_MAX_VALUE_LEN` → impossible length.
3. Fewer than `body_len` bytes remain → truncated body.
4. CRC32 of body ≠ stored CRC → CRC mismatch.
5. `type` not in {1, 2} → unknown type.
6. `klen == 0`, `klen > MAX`, `vlen > MAX`, or `9 + klen + vlen != body_len` → inconsistent lengths.
7. `type == DEL` and `vlen != 0` → invalid tombstone.

Exactly 0 bytes remaining after the last record = clean end of file (not corruption).

### 8.5 Algorithms

**Open:**
1. Create data dir if missing.
2. Acquire lock file (`CSKV_ERR_LOCKED` if held).
3. Delete leftover `cskv.log.tmp` if it exists (crash during compaction/restore — the original log is still the valid one).
4. If `cskv.log` does not exist → create it, write header, commit.
5. Else validate header (`CSKV_ERR_BAD_FORMAT` if wrong magic/version), then run recovery.
6. Open log in append mode for future writes.

**Recovery (replay):**
```
offset = 8
while true:
    if remaining == 0: clean end; break
    try read+validate record at offset (rules in 8.4)
    if invalid:
        corruption_found = 1; reason = "<rule> at offset <offset>"
        bytes_discarded = file_size - offset
        truncate file to offset   (_chsize_s), commit
        break
    apply record to hashtable: SET → put (overwrite), DEL → remove
    records_replayed++; offset += 8 + body_len
bytes_valid = offset
```
Never `abort()` on bad data. Always free partially read buffers.

**Append (SET/DEL):**
1. Validate arguments.
2. Encode the record into one contiguous buffer.
3. `fwrite` the whole buffer; check the return value.
4. If sync mode = ALWAYS: `fflush` + `_commit`. Check both.
5. **Only after the write succeeds**, update the hash table. (If the write fails the in-memory state must not change.)
6. If `fwrite` partially failed: attempt to truncate back to the previous size, return `CSKV_ERR_IO`.

**Compaction:**
1. Open `cskv.log.tmp`, write header.
2. For each live key in the hash table, write a SET record.
3. `fflush` + `_commit` + close tmp.
4. Close the current log handle.
5. `MoveFileExA(tmp, log, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.
6. Reopen log for append; reset `log_records = live_keys`, `dead_records = 0`.
7. If any step before 5 fails: delete tmp, reopen the original log, return error. **The original log is never modified before the atomic move.**

**Snapshot file format:**
| Field | Size | Description |
|---|---|---|
| magic | 4 | `"CSNP"` |
| version | 2 | `1` |
| reserved | 2 | `0` |
| entry_count | 8 | number of entries |
| entries | … | each: `klen(4) vlen(4) key value` |
| file_crc | 4 | CRC32 over **everything before this field** |

**Snapshot save:** write to `<path>.tmp`, flush + commit, close, `MoveFileExA` to `<path>`.

**Restore:**
1. Read the entire snapshot; verify magic, version, entry_count consistency, and file CRC. If anything fails → return `CSKV_ERR_CORRUPT`, **current state untouched**.
2. Build a new hash table from it.
3. Write a new log (`cskv.log.tmp`) containing SET records for all entries, commit, atomic move over `cskv.log`.
4. Swap in the new hash table, free the old one.

### 8.6 Hash table (`hashtable.c`)
- Separate chaining, initial capacity 64 buckets, hash = **FNV-1a 64-bit**.
- Resize (double) when `count / capacity > 0.75`.
- Table **owns copies** of keys and values (`malloc` + `memcpy`); `put` on an existing key frees the old value.
- API: `ht_create`, `ht_destroy`, `ht_put`, `ht_get` (returns pointer into table, no copy), `ht_remove`, `ht_count`, `ht_foreach`.
- `cskv_keys` collects key pointers into an array, `qsort`s them (memcmp, then length), then calls the callback.

### 8.7 Platform layer (`platform.h`)
All Windows-specific code lives here so the rest stays plain C:
```c
int  plat_mkdir_p(const char *path);
int  plat_file_exists(const char *path);
int  plat_remove(const char *path);
int  plat_fsync(FILE *fp);                         /* fflush + _commit */
int  plat_truncate(FILE *fp, int64_t size);        /* _chsize_s */
int  plat_atomic_replace(const char *src, const char *dst); /* MoveFileExA */
int64_t plat_file_size(FILE *fp);                  /* _fseeki64/_ftelli64 */
void *plat_lock_acquire(const char *lock_path);    /* returns HANDLE or NULL */
void  plat_lock_release(void *handle);
double plat_now_ms(void);                          /* QueryPerformanceCounter */
void plat_stdin_binary_if_piped(void);             /* CLI only: _setmode(O_BINARY) for pipes */
```
Every function returns 0 on success / non-zero on failure (or NULL) and never prints.

### 8.8 CLI (`cli/main.c`)
**Usage:**
```
cskv.exe [--data-dir DIR] [--sync always|none] [--json] [--help]
```

**Commands (case-insensitive command word):**
| Command | Effect |
|---|---|
| `SET <key> <value...>` | store |
| `GET <key>` | print value |
| `DEL <key>` | delete |
| `KEYS` | list keys |
| `STATS` | print statistics |
| `COMPACT` | compact log |
| `SNAPSHOT <path>` | save snapshot |
| `RESTORE <path>` | restore snapshot |
| `HELP` | list commands |
| `EXIT` / `QUIT` | close cleanly |

**Human mode (default):** prompt `cskv> `, responses like `OK`, `"Ahmed"`, `(nil)`, `ERR key too long (max 256 bytes)`.
On startup print the recovery summary, e.g.
`Recovered 1532 records in 12.4 ms. Corruption detected: CRC mismatch at offset 48211 — discarded 37 bytes.`

**JSON mode (`--json`) — used by the GUI and integration tests:**
- No prompt, no banners. Exactly **one JSON object per line** on stdout for every command. stdout is flushed after every line.
- On startup, the first line is the recovery report:
  `{"event":"recovery","records":1532,"bytes_valid":48211,"bytes_discarded":37,"corruption":true,"reason":"CRC mismatch at offset 48211","ms":12.4}`
- Responses:
  - `{"ok":true}`
  - `{"ok":true,"value":"Ahmed"}`
  - `{"ok":false,"error":"NOT_FOUND","message":"key not found"}`
  - `{"ok":true,"keys":["age","name"]}`
  - `{"ok":true,"stats":{"live_keys":2,"log_bytes":120,"log_records":5,"dead_records":3}}`
- Strings must be JSON-escaped (`"`, `\`, control chars → `\uXXXX`). Write a small `json_escape()` helper; no external JSON library.
- Max input line: 1 MiB + 300 bytes. Longer lines → `{"ok":false,"error":"INVALID_ARG","message":"line too long"}` and the rest of the line is discarded.

**Exit codes:** 0 normal, 1 bad arguments, 2 could not open store (locked / IO / bad format).

### 8.9 Corruption tool (`tools/cskv_corrupt.c`)
```
cskv_corrupt.exe <logfile> truncate <N>        # remove last N bytes
cskv_corrupt.exe <logfile> flip <offset>       # XOR one byte with 0xFF
cskv_corrupt.exe <logfile> garbage <N>         # append N random bytes
```
Used by tests and by the GUI "Simulate Crash" button. Prints what it did.

### 8.10 Coding conventions
- C11, `-std=c11 -Wall -Wextra -Wpedantic -Werror`.
- Every `malloc`/`realloc`/`fopen`/`fwrite`/`fread` return value checked.
- Every function that can fail returns `cskv_status` (engine) or int (platform).
- No global mutable state; everything hangs off the `cskv` struct.
- The engine (`src/`) **never prints**. Only `cli/` and `tools/` print.
- One owner per allocation; document ownership in header comments.
- Functions ≤ ~60 lines; comment every non-obvious algorithm step.
- Names: `snake_case`, module prefixes (`ht_`, `rec_`, `log_`, `plat_`, `cskv_`).

### 8.11 Makefile targets
```
make            # build build/libcskv.a, build/cskv.exe, build/cskv_corrupt.exe
make test       # build + run build/cskv_tests.exe (Unity) then pytest tests/integration
make gui        # python gui/app.py (after make)
make memcheck   # drmemory -- build/cskv_tests.exe
make clean      # remove build/
```
Use `CC ?= gcc`. Link nothing beyond the C runtime (MoveFileEx etc. are in kernel32, linked by default).

---

## 9. GUI Specification (`gui/`)

### 9.1 How it talks to the engine
- `engine_client.py` starts `build/cskv.exe --json --data-dir <dir>` with `subprocess.Popen(stdin=PIPE, stdout=PIPE, stderr=PIPE, text=True, bufsize=1)`.
- Sends one command per line, reads exactly one JSON line back (`json.loads`).
- Reads the startup recovery line first.
- Reads stdout on a background thread and passes results to Tkinter via `queue.Queue` + `root.after()` polling so the UI never freezes.
- If the process exits unexpectedly or `cskv.exe` is missing → show an error banner and a "Restart engine" button; never crash the GUI.

### 9.2 Layout (single window, ~1000×650)
1. **Top bar:** data directory picker, engine status light (green running / red stopped), Start / Stop / Restart buttons.
2. **Left panel — Operations:** Key entry, Value entry (multi-line), buttons SET / GET / DELETE. Result label below.
3. **Centre — Data table:** `ttk.Treeview` with columns Key | Value (truncated to 80 chars) | Size. Refreshes after every change (KEYS + GET).
4. **Right panel — Stats:** live keys, log size, records, dead records, dead ratio bar, last recovery summary.
5. **Bottom — Maintenance:** COMPACT (shows size before → after), SNAPSHOT (save dialog), RESTORE (open dialog, confirmation dialog).
6. **Crash Lab panel:** dropdown (Truncate last N bytes / Flip byte at offset / Append garbage), N/offset entry, **"Simulate Crash"** button.
7. **Log console:** scrolling text with every command sent and response received (timestamped).

### 9.3 Validation in the GUI
- Empty key, key > 256 bytes, key with whitespace → red message, nothing sent.
- Value > 1 MiB → rejected.
- RESTORE / Simulate Crash require a confirmation dialog.
- All engine errors shown in red with the engine's message.

### 9.4 Demo scenario (must work end to end)
1. Start engine on an empty data dir → recovery: 0 records.
2. SET 5 keys, overwrite 2, delete 1 → table shows 4 keys; stats shows dead records.
3. COMPACT → log size shrinks, dead records = 0.
4. SNAPSHOT to `backup.snap`.
5. SET 2 more keys.
6. **Simulate Crash** (truncate last 5 bytes) → engine is killed, file damaged, engine restarted automatically → recovery banner turns orange: "Corruption detected — discarded N bytes", the last key is gone, every earlier key is intact.
7. RESTORE `backup.snap` → table matches step 4.

### 9.5 "Simulate Crash" implementation
1. `process.kill()` the engine (hard stop, no clean shutdown).
2. Run `cskv_corrupt.exe <log> <mode> <arg>`.
3. Start the engine again and display the recovery JSON prominently.

---

## 10. Test Plan

### 10.1 Unit tests (Unity, `tests/`)
| Module | Cases |
|---|---|
| crc32 | Known vector: CRC32("123456789") = `0xCBF43926`; empty input = `0`; incremental = one-shot |
| hashtable | put/get; overwrite frees old; remove; remove missing; resize past 10,000 keys; binary keys with `\0`; destroy with entries (no leaks) |
| record | encode→decode round trip; each validation rule in 8.4 rejected individually |
| recovery | empty log; header only; 1000 records; truncated header; truncated body; CRC flip in middle record (earlier kept, later dropped); garbage appended; wrong magic → BAD_FORMAT; file truncated to valid offset after recovery |
| kv | set/get/del; get missing; del missing writes nothing; empty value allowed; max-size key/value; over-size rejected; NULL args rejected |
| compact | data identical before/after; size shrinks; leftover `.tmp` removed on open; compaction on empty store |
| snapshot | save/restore round trip; corrupted snapshot rejected and state unchanged; snapshot of empty store |
| lock | second `cskv_open` on same dir → `CSKV_ERR_LOCKED` |

### 10.2 Integration tests (pytest, `tests/integration/`)
- Every CLI command in `--json` mode, success and failure.
- Kill-during-writes: start engine, send 1000 SETs, kill process, restart, verify every acknowledged key exists.
- Corruption via `cskv_corrupt.exe` (all three modes) → recovery JSON reports it, earlier data intact, new writes work after.
- Restart persistence: SET, EXIT, start again, GET.
- Malformed input: empty line, unknown command, missing arguments, very long line.
- Missing data dir is created; read-only/locked dir gives exit code 2.

### 10.3 Evidence for the report
Save test output to `docs/test_results.txt`; screenshots of the demo scenario to `docs/screenshots/`; Dr. Memory summary showing 0 leaks.

---

## 11. Report Outline (required by course)
1. Title page: project title "CrashSafeKV" + project number **53**, member names + IDs, course info, each member's responsibilities.
2. Project overview: problem (data loss on crash), purpose, scope, intended users, system description.
3. Requirements & use cases: section 5 of this file + user scenarios (section 9.4); implemented vs out-of-scope.
4. Design & architecture: diagram (8.1), module responsibilities, data flow for SET / recovery / compaction, file formats (8.4, snapshot), hash table design, Windows durability APIs.
5. Implementation details: tools (section 6), key decisions and trade-offs (append-only vs in-place; sync always vs none; tombstones; atomic rename), known limitations.
6. GUI design & operation: screenshots of every panel, workflows, validation, how it talks to the engine (JSON over pipes).
7. Testing & results: plan, environment, cases table, expected vs actual, defects found and fixed, remaining issues, evidence.
8. Build & run instructions: copied from README.
9. Conclusion & future work: e.g. background compaction, multi-threading, range queries, Linux port.
10. References & appendices: Unity framework, CRC32 reference, Windows API docs, sample inputs/outputs.

---

## 12. Suggested Team Split (10 members)
| Member | Responsibility |
|---|---|
| 1 | Hash table + memory ownership |
| 2 | CRC32 + record encode/decode |
| 3 | Log append + platform layer (durability, lock) |
| 4 | Recovery + corruption handling |
| 5 | Compaction + snapshot/restore |
| 6 | CLI (human + JSON modes) + error messages |
| 7 | GUI (Tkinter + engine client) |
| 8 | Corruption tool + crash/kill integration tests |
| 9 | Unit tests + Makefile + Dr. Memory |
| 10 | Report, README, diagrams, screenshots |

With 8 members: merge 1+2 and 8+9.

---

## 13. Instructions for Claude Code

1. **Build in the order of section 4.** Do not start the GUI before the `--json` CLI passes its integration tests.
2. **Respect the constraints:** C11, Windows, MinGW, no networking, no Linux APIs, no third-party C libraries except vendored Unity.
3. **Write tests alongside each module**, then run `make test`. A module is done only when its tests pass with `-Werror`.
4. **Never change the file formats** (8.4 and snapshot) without updating this document.
5. **Keep the engine silent** — no `printf` in `src/`.
6. **Durability order matters:** write → flush → commit → update memory → return OK.
7. **Atomic replacement only via `plat_atomic_replace`**; never overwrite `cskv.log` directly.
8. When finished with a phase, update `README.md` and `KNOWN_ISSUES.md`.
9. Definition of done for the whole project:
   - `make clean && make && make test` passes on a clean Windows machine with MSYS2.
   - Demo scenario 9.4 works from the GUI.
   - Dr. Memory reports 0 leaks.
   - README lets a new person build and run in under 10 minutes.
