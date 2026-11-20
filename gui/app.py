"""CrashSafeKV control panel.

Launches the real build/cskv.exe and drives it through its --json mode
(see engine_client.py). Run from the project root:  python gui/app.py
"""
import ctypes
import json
import sys
import tkinter as tk
import traceback
from pathlib import Path
from tkinter import filedialog, messagebox

import rules
import theme as th
from engine_client import ROOT, EngineClient, EngineError, run_corrupt
from panels import (ConsolePanel, CrashLabPanel, DataPanel, MaintenancePanel,
                    OperationsPanel, StatsPanel)
from widgets import Banner, FlatButton, StatusPill, TabbedCard

POLL_MS = 30


def enable_high_dpi():
    """Render sharply on scaled Windows displays instead of blurry-bitmap."""
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (AttributeError, OSError):
        pass


def use_dark_title_bar(root):
    """Windows 10 20H1+ / 11: dark window frame to match the app."""
    try:
        root.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
        value = ctypes.c_int(1)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, 20, ctypes.byref(value),
                                                   ctypes.sizeof(value))
    except (AttributeError, OSError):
        pass


def work_area(root):
    """Screen area not covered by the taskbar: (left, top, width, height)."""
    try:
        rect = (ctypes.c_long * 4)()
        if ctypes.windll.user32.SystemParametersInfoW(0x0030, 0, rect, 0):  # SPI_GETWORKAREA
            return rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]
    except (AttributeError, OSError):
        pass
    return 0, 0, root.winfo_screenwidth(), root.winfo_screenheight()


def place_window(root, theme):
    """Default 1280x800 (scaled), shrunk to fit the work area, centred."""
    p = theme.px
    left, top, avail_w, avail_h = work_area(root)
    title_bar = p(32)
    width = min(p(1280), avail_w - p(24))
    height = min(p(800), avail_h - title_bar - p(16))
    x = left + (avail_w - width) // 2
    y = top + max(0, (avail_h - height - title_bar) // 2)
    root.geometry(f"{width}x{height}+{x}+{y}")
    root.minsize(min(p(1000), width), min(p(620), height))


def make_icon(root):
    """32x32 window icon drawn in code: three stacked log records on a
    rounded accent tile (no image files to ship)."""
    icon = tk.PhotoImage(width=32, height=32)
    icon.put(th.ACCENT, to=(3, 1, 29, 31))
    icon.put(th.ACCENT, to=(1, 3, 31, 29))
    icon.put(th.ACCENT, to=(2, 2, 30, 30))
    for top, right in ((8, 24), (14, 20), (20, 24)):
        icon.put(th.ON_ACCENT, to=(8, top, right, top + 4))
    root.iconphoto(True, icon)
    return icon


def sentence(text):
    """Upper-case only the first letter ("I/O error" stays "I/O error";
    str.capitalize() would turn it into "I/o error")."""
    return text[:1].upper() + text[1:]


def compact_json(obj):
    """Same shape as the engine prints it: no spaces after separators."""
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":"))


def short_path(path, limit=56):
    """Show folders inside the project relative to it (".\\data")."""
    try:
        text = ".\\" + str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        text = str(path)
    return text if len(text) <= limit else "…" + text[-(limit - 1):]


class App:
    def __init__(self, root):
        self.root = root
        self.theme = th.Theme(root)
        self.client = EngineClient()
        self.data_dir = ROOT / "data"
        self.generation = 0            # bumps on every refresh; drops stale replies
        self.startup_failed = False
        root.title("CrashSafeKV")
        root.configure(bg=th.BG)
        self._icon = make_icon(root)          # keep a reference alive
        root.report_callback_exception = self._on_tk_error
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        self._build()
        self._set_engine_controls(False)
        root.after(POLL_MS, self._poll)
        root.after(100, self.start_engine)

    # ---- layout ----------------------------------------------------------

    def _build(self):
        t, p, root = self.theme, self.theme.px, self.root
        root.columnconfigure(0, weight=1)
        root.rowconfigure(2, weight=1)
        self._build_header().grid(row=0, column=0, sticky="ew")

        self.banner = Banner(root, t)
        self.banner.grid(row=1, column=0, sticky="ew", padx=p(16), pady=(p(12), 0))
        self.banner.grid_remove()

        body = tk.Frame(root, bg=th.BG)
        body.grid(row=2, column=0, sticky="nsew", padx=p(16), pady=p(12))
        body.rowconfigure(0, weight=1)
        body.columnconfigure(0, minsize=p(330))
        body.columnconfigure(1, weight=1)
        body.columnconfigure(2, minsize=p(300))

        # Left: one card, three tabs - keeps every tool reachable on small screens.
        side = TabbedCard(body, t)
        side.grid(row=0, column=0, sticky="nsew")
        self.ops = OperationsPanel(side.pages, t, self.on_set, self.on_get,
                                   self.on_delete)
        self.maint = MaintenancePanel(side.pages, t, self.on_compact,
                                      self.on_snapshot, self.on_restore)
        self.crash = CrashLabPanel(side.pages, t, self.on_simulate_crash)
        side.add("ops", "Key / value", self.ops)
        side.add("maint", "Maintenance", self.maint)
        side.add("crash", "Crash lab", self.crash)
        self.side = side

        # Centre: the data table above the activity log.
        centre = tk.Frame(body, bg=th.BG)
        centre.grid(row=0, column=1, sticky="nsew", padx=p(12))
        centre.rowconfigure(0, weight=1)
        centre.columnconfigure(0, weight=1)
        self.data = DataPanel(centre, t, self.ops.fill, self.refresh)
        self.data.grid(row=0, column=0, sticky="nsew")
        self.console = ConsolePanel(centre, t)
        self.console.grid(row=1, column=0, sticky="ew", pady=(p(12), 0))

        # Right: storage metrics and the last recovery.
        self.stats = StatsPanel(body, t)
        self.stats.grid(row=0, column=2, sticky="new")

    def _build_header(self):
        t, p = self.theme, self.theme.px
        outer = tk.Frame(self.root, bg=th.BG)
        bar = tk.Frame(outer, bg=th.BG)
        bar.pack(fill="x", padx=p(16), pady=(p(12), p(11)))
        tk.Frame(outer, bg=th.BORDER, height=1).pack(fill="x")

        tk.Label(bar, text="CrashSafeKV", bg=th.BG, fg=th.TEXT,
                 font=t.font["brand"]).pack(side="left")
        tk.Frame(bar, bg=th.BORDER, width=1, height=p(20)).pack(side="left", padx=p(14))
        tk.Label(bar, text="Data directory", bg=th.BG, fg=th.TEXT_3,
                 font=t.font["small"]).pack(side="left")
        self.dir_label = tk.Label(bar, text=short_path(self.data_dir), bg=th.BG,
                                  fg=th.TEXT_2, font=t.font["mono_small"])
        self.dir_label.pack(side="left", padx=(p(8), p(4)))
        self.dir_button = FlatButton(bar, t, "Change…", self.on_change_dir,
                                     "ghost", small=True)
        self.dir_button.pack(side="left")

        self.restart_button = FlatButton(bar, t, "Restart", self.restart_engine,
                                         small=True)
        self.restart_button.pack(side="right")
        self.stop_button = FlatButton(bar, t, "Stop", self.stop_engine, small=True)
        self.stop_button.pack(side="right", padx=p(6))
        self.start_button = FlatButton(bar, t, "Start", self.start_engine,
                                       "primary", small=True)
        self.start_button.pack(side="right")
        self.pill = StatusPill(bar, t)
        self.pill.pack(side="right", padx=p(16))
        self.pill.set("stopped", "Stopped")
        return outer

    def _set_engine_controls(self, running):
        self.start_button.set_enabled(not running)
        self.stop_button.set_enabled(running)
        self.restart_button.set_enabled(running)
        for button in (self.ops.buttons + self.maint.buttons + [self.crash.button]):
            button.set_enabled(running)

    # ---- engine lifecycle ------------------------------------------------

    def start_engine(self):
        if self.client.running:
            return
        self.startup_failed = False
        self.pill.set("starting", "Starting…")
        try:
            self.client.start(self.data_dir, self._on_startup, self._on_exit)
        except EngineError as exc:
            self._engine_down("error", "Not available")
            self.banner.show("error", "The engine could not be started", str(exc),
                             action=("Try again", self.start_engine))
            self.console.log("error", str(exc))
            return
        self.console.log("info", f"Started cskv.exe (pid {self.client.pid}) "
                                 f"on {self.data_dir}")

    def stop_engine(self):
        self.client.stop()
        self.console.log("info", "Engine stopped")
        self._engine_down("stopped", "Stopped")

    def restart_engine(self):
        self.stop_engine()
        self.start_engine()

    def _engine_down(self, state, text):
        self.pill.set(state, text)
        self._set_engine_controls(False)

    def _on_startup(self, msg):
        self.console.log("event", compact_json(msg))
        if msg.get("event") != "recovery":           # store could not be opened
            self.startup_failed = True
            self._engine_down("error", "Failed to open")
            self.banner.show("error", "The data directory could not be opened",
                             f"{msg.get('message', 'unknown error')}. "
                             "Another program may be using it.",
                             action=("Try again", self.start_engine))
            return
        self.pill.set("running", f"Running  ·  pid {self.client.pid}")
        self._set_engine_controls(True)
        self.stats.set_recovery(msg)
        self._show_recovery_banner(msg)
        self.refresh()

    def _show_recovery_banner(self, rec):
        if rec["corruption"]:
            self.banner.show(
                "warning",
                f"Corruption detected — discarded {rec['bytes_discarded']} bytes",
                f"{rec['reason']}. {rec['records']} valid records were recovered; "
                "everything before the damaged record is intact.")
        else:
            self.banner.show(
                "success", "Store opened",
                f"Replayed {rec['records']} records in {rec['ms']:.1f} ms. "
                "No corruption found.", auto_hide_ms=6000)

    def _on_exit(self, code, stderr):
        if self.startup_failed:                     # already reported
            return
        self._engine_down("error", "Stopped unexpectedly")
        detail = f"Exit code {code}." + (f" {stderr.strip()}" if stderr.strip() else "")
        self.console.log("error", f"Engine exited unexpectedly ({detail})")
        self.banner.show("error", "The engine stopped unexpectedly", detail,
                         action=("Restart engine", self.start_engine))

    def _poll(self):
        self.client.poll()
        self.root.after(POLL_MS, self._poll)

    # ---- talking to the engine -------------------------------------------

    def send(self, command, on_reply, quiet=False):
        """Send one command; log it and its reply. quiet = background refresh."""
        if not self.client.running:
            self.ops.show_result("error", "The engine is not running",
                                 "Start it from the top bar.")
            return
        self.console.log("send_quiet" if quiet else "send", command)

        def handle(reply):
            ok = reply.get("ok", False)
            kind = ("ok_quiet" if quiet else "ok") if ok else "err"
            self.console.log(kind, compact_json(reply))
            on_reply(reply)

        self.client.send(command, handle)

    def refresh(self):
        """Reload stats and the whole table (KEYS, then GET for each key)."""
        self.generation += 1
        gen = self.generation
        self.send("STATS", lambda r: r.get("ok") and self.stats.set_stats(r["stats"]),
                  quiet=True)

        def on_keys(reply):
            if gen != self.generation or not reply.get("ok"):
                return
            keys, values = reply["keys"], {}
            if not keys:
                self.data.set_rows([])
            for key in keys:
                def on_value(r, key=key):
                    if gen != self.generation:
                        return
                    values[key] = r.get("value") if r.get("ok") else None
                    if len(values) == len(keys):
                        self.data.set_rows([(k, values[k]) for k in keys
                                            if values[k] is not None])
                self.send(f"GET {key}", on_value, quiet=True)

        self.send("KEYS", on_keys, quiet=True)

    # ---- key / value operations ------------------------------------------

    def _validated(self, need_value):
        key = self.ops.key.get()
        error = rules.check_key(key)
        value = self.ops.value.get() if need_value else ""
        if error is None and need_value:
            error = rules.check_value(value)
        if error:
            self.ops.show_result("error", error)
            return None, None
        return key, value

    def on_set(self):
        key, value = self._validated(need_value=True)
        if key is None:
            return

        def done(reply):
            if reply.get("ok"):
                size = rules.fmt_bytes(rules.byte_len(value))
                self.ops.show_result("success", f"Saved “{key}”", size)
                self.refresh()
                self.root.after(150, lambda: self.data.select(key))
            else:
                self.ops.show_result("error", sentence(reply["message"]))
        self.send(f"SET {key} {value}", done)

    def on_get(self):
        key, _ = self._validated(need_value=False)
        if key is None:
            return

        def done(reply):
            if reply.get("ok"):
                self.ops.value.set(reply["value"])
                self.ops.show_result("info", f"“{key}”", reply["value"])
                self.data.select(key)
            elif reply.get("error") == "NOT_FOUND":
                self.ops.show_result("warning", "Not found",
                                     f"No value is stored under “{key}”.")
            else:
                self.ops.show_result("error", sentence(reply["message"]))
        self.send(f"GET {key}", done)

    def on_delete(self):
        key, _ = self._validated(need_value=False)
        if key is None:
            return

        def done(reply):
            if reply.get("ok"):
                self.ops.show_result("success", f"Deleted “{key}”")
                self.refresh()
            elif reply.get("error") == "NOT_FOUND":
                self.ops.show_result("warning", "Nothing to delete",
                                     f"“{key}” does not exist.")
            else:
                self.ops.show_result("error", sentence(reply["message"]))
        self.send(f"DEL {key}", done)

    # ---- maintenance -----------------------------------------------------

    def on_compact(self):
        def done(reply):
            if not reply.get("ok"):
                self.maint.set_status("error", sentence(reply["message"]))
                return
            before, after = reply["bytes_before"], reply["bytes_after"]
            saved = (1 - after / before) if before else 0
            self.maint.set_status(
                "success", f"Compacted: {rules.fmt_bytes(before)} → "
                           f"{rules.fmt_bytes(after)}  ({saved:.0%} smaller)")
            self.refresh()
        self.send("COMPACT", done)

    def on_snapshot(self):
        path = filedialog.asksaveasfilename(
            parent=self.root, title="Save snapshot", initialdir=ROOT,
            initialfile="backup.snap", defaultextension=".snap",
            filetypes=[("CrashSafeKV snapshot", "*.snap"), ("All files", "*.*")])
        if not path:
            return

        def done(reply):
            if reply.get("ok"):
                self.maint.set_status("success", f"Snapshot saved to {Path(path).name}")
            else:
                self.maint.set_status("error", sentence(reply["message"]))
        self.send(f"SNAPSHOT {Path(path)}", done)

    def on_restore(self):
        path = filedialog.askopenfilename(
            parent=self.root, title="Restore snapshot", initialdir=ROOT,
            filetypes=[("CrashSafeKV snapshot", "*.snap"), ("All files", "*.*")])
        if not path:
            return
        if not messagebox.askyesno(
                "Restore snapshot",
                f"Replace ALL current data with the contents of "
                f"{Path(path).name}?\n\nKeys added since the snapshot will be lost.",
                icon="warning", default="no", parent=self.root):
            return

        def done(reply):
            if reply.get("ok"):
                self.maint.set_status("success", f"Restored from {Path(path).name}")
                self.banner.show("success", "Snapshot restored",
                                 f"All data now matches {Path(path).name}.",
                                 auto_hide_ms=6000)
                self.refresh()
            elif reply.get("error") == "CORRUPT":
                self.maint.set_status("error", "This file is damaged or is not a "
                                               "snapshot. Your data was not changed.")
            else:
                self.maint.set_status("error", sentence(reply["message"]))
        self.send(f"RESTORE {Path(path)}", done)

    # ---- crash lab -------------------------------------------------------

    def on_simulate_crash(self, mode, amount):
        what = {"truncate": f"cut the last {amount} bytes off",
                "flip": f"invert the byte at offset {amount} of",
                "garbage": f"append {amount} random bytes to"}[mode]
        if not messagebox.askyesno(
                "Simulate crash",
                f"The engine will be killed without a clean shutdown, then the "
                f"tool will {what} cskv.log.\n\nThe engine restarts right after "
                "and recovers what it can. Continue?",
                icon="warning", default="no", parent=self.root):
            return
        self.console.log("event", f"Simulated crash: killing pid {self.client.pid}")
        self.client.kill()
        self._engine_down("stopped", "Killed")
        ok, message = run_corrupt(self.data_dir / "cskv.log", mode, amount)
        self.console.log("event" if ok else "error", f"cskv_corrupt: {message}")
        if not ok:
            self.banner.show("error", "The log could not be damaged", message)
        self.start_engine()

    # ---- misc ------------------------------------------------------------

    def on_change_dir(self):
        chosen = filedialog.askdirectory(parent=self.root, initialdir=self.data_dir,
                                         title="Choose data directory")
        if not chosen or Path(chosen) == self.data_dir:
            return
        self.client.stop()
        self.data_dir = Path(chosen)
        self.dir_label.configure(text=short_path(self.data_dir))
        self.data.set_rows([])
        self.start_engine()

    def _on_tk_error(self, exc_type, exc, tb):
        traceback.print_exception(exc_type, exc, tb)
        self.console.log("error", f"GUI error: {exc_type.__name__}: {exc}")

    def on_close(self):
        self.client.stop()
        self.root.destroy()


def main():
    enable_high_dpi()
    root = tk.Tk()
    app = App(root)
    place_window(root, app.theme)
    use_dark_title_bar(root)
    root.mainloop()


if __name__ == "__main__":
    sys.exit(main())
