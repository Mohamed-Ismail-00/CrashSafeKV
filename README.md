# CrashSafeKV

A durable append-only key-value storage engine in C11 for Windows
(Systems Programming project **#53**, extended with #66, #69 and #58).
Every change is appended to a log with a CRC32 checksum; on startup the log is replayed and a
damaged tail (from a crash or power loss) is detected and cut off. Compaction and snapshots
are atomic. A Tkinter GUI drives the real engine and can simulate a crash live.

| Document | Content |
|---|---|
| [PROJECT_EXPLANATION.md](PROJECT_EXPLANATION.md) | **start here**: the whole project explained step by step |
| [CLAUDE.md](CLAUDE.md) | full specification |
| [docs/REPORT.md](docs/REPORT.md) | project report draft (course outline) |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | limitations, defects found and fixed |
| [docs/test_results.txt](docs/test_results.txt) | latest full test run |
| [docs/diagrams/](docs/diagrams) | architecture, file formats, recovery, write/compaction |
| [docs/screenshots/](docs/screenshots) | demo scenario, step by step |

## Quick start (about 10 minutes on a fresh Windows machine)

1. Install [MSYS2](https://www.msys2.org/) to the default folder `C:\msys64`.
2. Open the **MSYS2 UCRT64** terminal from the Start menu and install the compiler and make:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc make
   ```
3. Install [Python 3.10+](https://www.python.org/downloads/) (keep the default "tcl/tk and
   IDLE" option, which provides Tkinter), then install pytest:
   ```
   python -m pip install pytest
   ```
4. Open **PowerShell** in the project folder and put the MSYS2 tools first in `PATH`
   (for this window only):
   ```powershell
   $env:Path = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:Path
   ```
5. Build, test and start the GUI:
   ```
   make
   make test
   make gui
   ```

## Build and test

All `make` targets (run in the PowerShell window from step 4):

```
make            # build/libcskv.a, build/cskv.exe, build/cskv_corrupt.exe
make test       # Unity unit tests, then pytest integration tests
make leakcheck  # unit tests with counting malloc/free: fails on any memory leak
make memcheck   # unit tests under Dr. Memory (if installed and working, see KNOWN_ISSUES.md)
make clean      # remove build/
```

### Tests

| Suite | Count | What it covers |
|---|---|---|
| Unity unit tests (`tests/test_*.c`) | 57 | CRC32, hash table, record rules, recovery, engine API, compaction, snapshots, lock, fuzzing with random/damaged files |
| pytest (`tests/integration/`) | 60 | every CLI command, invalid input, kill during writes, all corruption modes, exit codes, GUI engine client and input rules |
| Leak check (`make leakcheck`) | 57 | every unit test must free all of its allocations |

The latest full run is saved in [docs/test_results.txt](docs/test_results.txt).

> If `gcc` stops with an error but no message, another folder earlier in `PATH`
> ships conflicting DLLs, or `TMP`/`TEMP` point to a folder you cannot write to.

## Using the CLI

```
build\cskv.exe [--data-dir DIR] [--sync always|none] [--json] [--help]
```

| Command | Effect |
|---|---|
| `SET <key> <value...>` | store a value (rest of the line, spaces allowed) |
| `GET <key>` | print the value, or `(nil)` |
| `DEL <key>` | delete a key |
| `KEYS` | list keys, sorted |
| `STATS` | live keys, log size, records, dead records, last recovery |
| `COMPACT` | rewrite the log with live keys only |
| `SNAPSHOT <path>` / `RESTORE <path>` | save / restore a checksummed backup |
| `HELP`, `EXIT`, `QUIT` | |

Keys on the command line are printable ASCII without spaces (1-256 bytes); values up to 1 MiB.
The default data directory is `.\data`. Exit codes: 0 normal, 1 bad arguments, 2 store could not
be opened (locked, I/O error, bad format).

Example session (`samples/demo_commands.txt` can be piped in):

```
> build\cskv.exe
Recovered 0 records in 0.0 ms.
cskv> SET name Ahmed Ali
OK
cskv> GET name
"Ahmed Ali"
```

`--json` prints one JSON object per line (used by the GUI and the tests), for example
`{"ok":true,"value":"Ahmed Ali"}` or `{"ok":false,"error":"NOT_FOUND","message":"key not found"}`.
The first line is always the recovery report.

### Damaging a log on purpose

```
build\cskv_corrupt.exe data\cskv.log truncate 5     # remove the last 5 bytes
build\cskv_corrupt.exe data\cskv.log flip 50        # invert the byte at offset 50
build\cskv_corrupt.exe data\cskv.log garbage 40     # append 40 random bytes
```

Start `cskv.exe` again afterwards to see the corruption detected and cut off.

## GUI

```
make gui              # or: python gui/app.py   (after make)
```

The control panel starts `build\cskv.exe --json` on `.\data` (change it from the top bar) and
talks to it over pipes. Standard library only (Tkinter).

| Area | What it does |
|---|---|
| Top bar | data directory, engine status, Start / Stop / Restart |
| Banner | recovery result after every start (green clean, amber corruption), engine errors |
| Left: Key / value | Set / Get / Delete with live byte counters and input checks |
| Left: Maintenance | Compact (shows size before and after), Snapshot, Restore (with confirmation) |
| Left: Crash lab | kill the engine, damage `cskv.log` with `cskv_corrupt.exe`, restart, watch recovery |
| Centre | all keys with value preview and size, filter box; activity log of every command and reply |
| Right | live keys, log size, records, dead records, dead-space bar, last startup details |

Keyboard: Enter in Key runs Get, Enter in Value runs Set. Clicking a row loads it into the editor.

### Demo scenario (CLAUDE.md 9.4)

1. Start on an empty data directory: banner shows 0 records replayed.
2. Set 5 keys, overwrite 2, delete 1: table shows 4 keys, Storage shows 4 dead records.
3. Maintenance, then Compact: log shrinks, dead records drop to 0.
4. Maintenance, then Snapshot, save as `backup.snap`.
5. Set 2 more keys.
6. Crash lab, Truncate 5, then Simulate crash: amber banner "Corruption detected - discarded N bytes";
   the last key is gone, all earlier keys are intact.
7. Maintenance, then Restore `backup.snap`: the table matches step 4.

## Layout

```
include/   public API (cskv.h) and module headers
src/       engine (never prints)
cli/       cskv.exe: main.c (loop), commands.c (parsing), output.c (human/JSON output)
tools/     cskv_corrupt.c
tests/     Unity unit tests (tests/unity is vendored, MIT licence), memtrack.* for make leakcheck
tests/integration/   pytest tests that drive the real executables
gui/       app.py (window + wiring), panels.py, widgets.py, theme.py,
           engine_client.py (process + pipes), rules.py (input checks)
samples/   demo command script
docs/      REPORT.md, test_results.txt, diagrams/ (SVG), screenshots/ (PNG)
```

## Third-party code

- [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.0 test framework, MIT licence
  (`tests/unity/LICENSE.txt`).
