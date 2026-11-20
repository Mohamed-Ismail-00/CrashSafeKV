# Known Issues

## Memory checking (Dr. Memory)

- Dr. Memory could not be run on the development machine (Windows 11 Pro, build 22631). Both
  the 2.6.0 release and the newest build (2.6.20434), 64-bit and 32-bit, stop with
  "Dr. Memory internal crash" or "failed to start the target application" **even on a
  five-line hello-world program**, so the cause is the tool/OS combination (likely CPU
  shadow-stack protection or security software), not CrashSafeKV.
- Replacement evidence: `make leakcheck` rebuilds the engine and all unit tests with counting
  `malloc`/`calloc`/`realloc`/`free` wrappers (`tests/memtrack.*`). Every test must end with
  the same number of live allocations it started with, otherwise it fails. It reports
  **0 leaks for all 57 unit tests**, and was shown to catch a leak by deliberately removing
  one `free()` (the affected tests failed immediately).
- Limits of the replacement: it detects leaks and double frees, not out-of-bounds or
  uninitialised reads, and it covers the engine (`src/`) through the unit tests, not the CLI
  (`cli/`). `make memcheck` still runs Dr. Memory and should be used on a machine where it works.

## Not covered by an automated test

- "Read-only data directory gives exit code 2": Windows folder permissions are awkward to set up
  from a test. The same error path (store cannot be opened, exit code 2) is tested with a data
  directory that is actually a file, a locked directory and a bad-format log.
- The GUI window itself (layout, dialogs). Its engine client and input rules are tested
  (`tests/integration/test_gui_client.py`); the window was checked by running demo scenario 9.4
  end to end and by killing the engine from outside.

## CLI limitations

- An empty value cannot be stored from the CLI (`SET key` with nothing after it is rejected as
  missing a value). The engine API does support empty values.
- Spaces/tabs between the key and the value are skipped, so a value cannot start with a space.
- Keys must be printable ASCII without spaces (the engine itself accepts any bytes).
- Human mode prints values between quotes without escaping them.
- In an interactive console, Ctrl+Z then Enter still means "end of input" (closes the store).
  When input comes from a pipe (GUI, tests, scripts) every byte, including 0x1A, is data.

## GUI limitations

- The table is refreshed with `KEYS` followed by one `GET` per key after every change. This is
  instant for demo-sized stores but slows down with many thousands of keys.
- Because the GUI uses the line-based CLI protocol, values entered in the GUI cannot contain line
  breaks or start with a space (the GUI explains this before sending anything).
- The Crash lab mode selector is a segmented control (three buttons) instead of a dropdown; it
  offers the same three choices as CLAUDE.md 9.2.

## Design limitations

- Paths (data dir + file name) are limited to 259 characters (Windows `MAX_PATH`, ANSI APIs).
- If a `SET` record is written to disk but the following in-memory insert runs out of memory,
  `cskv_set` returns `CSKV_ERR_NO_MEMORY` although the value is durable; it appears after a restart.
- An empty (0-byte) `cskv.log` is treated as a new store and gets a fresh header. A file with
  1-7 bytes, or a wrong magic/version, is rejected with `CSKV_ERR_BAD_FORMAT`.
- `RESTORE` reads the whole snapshot file into memory before validating it, so restoring needs
  free RAM about the size of the snapshot (plus the new hash table).
- Snapshot paths are limited to 275 characters (the `.tmp` name must fit `MAX_PATH`).
- If the log cannot be reopened after a compaction/restore rename (e.g. the disk vanished), later
  writes return `CSKV_ERR_IO` until the store is reopened; no data on disk is lost.
- With `--sync none`, each write is still flushed to the OS (survives a process kill) but not
  committed to disk (may be lost on power failure).
- A lock failure is reported as `LOCKED` only when the lock file exists (another process holds
  it); any other failure to create it (for example no permission) is reported as `IO`.

## Environment notes

- `gcc` can fail with no useful message when another folder earlier in `PATH` provides
  incompatible DLLs, or when `TMP`/`TEMP` point to an unwritable folder ("Cannot create
  temporary file in C:\Windows\"). Put the MSYS2 folders first in `PATH` as the README shows.

## Status

All phases (0-7) are complete. `README.md` lists the build steps; `docs/REPORT.md` is the
report draft (team names and IDs still to be filled in).

## Defects found and fixed during testing (phase 6)

| Defect | Found by | Fix |
|---|---|---|
| A value containing byte 0x1A (Ctrl+Z) ended the session: Windows text-mode stdin reads it as end of file | `test_ctrl_z_byte_does_not_end_the_session`, random-input test | stdin switched to binary mode when it is a pipe (`plat_stdin_binary_if_piped`) |
| A data directory path that is actually a file was reported as "locked by another process" | `test_data_dir_that_is_a_file_gives_exit_code_2` | `plat_mkdir_p` checks that existing path parts are directories; `LOCKED` only when the lock file exists |
| Test hygiene: the random-input test could send `SNAPSHOT <random name>` and leave a snapshot file in the project folder | clean-copy check (phase 7) | integration tests start the engine with the test's temp folder as working directory |
| A UTF-8 byte order mark (added by PowerShell pipes and Notepad) made the first command "unknown" | checking a real record before upload | the BOM is removed from the first input line |
| GUI: error messages such as "I/O error" were shown as "I/o error" (`str.capitalize`) | final review | only the first letter is upper-cased (`sentence()` in `gui/app.py`) |
| GUI: after a refresh (e.g. Compact) the re-selected table row was loaded into the editor, overwriting unsaved typing | final review | rows load into the editor only on a user click or arrow key |

## Open defects

None known.
