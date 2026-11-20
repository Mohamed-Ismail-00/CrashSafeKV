"""The panels of the main window. Each panel only displays things and reports
user actions through callbacks; app.py decides what to send to the engine."""
import time
import tkinter as tk
from tkinter import ttk

import rules
import theme as th
from widgets import (Card, FlatButton, InputBox, Page, RatioBar, Segmented,
                     TextBox, field_label)

_RESULT_COLORS = {"success": th.GREEN, "error": th.RED,
                  "warning": th.AMBER, "info": th.TEXT}


class OperationsPanel(Page):
    """Key + value inputs with SET / GET / DELETE."""

    def __init__(self, parent, theme, on_set, on_get, on_delete):
        super().__init__(parent, theme)
        self._theme = theme
        p, body = theme.px, self.body

        self._key_hint = field_label(body, theme, "Key")
        self.key = InputBox(body, theme, "e.g. user:42", mono=True)
        self.key.pack(fill="x")
        self.key.bind_key("<Return>", lambda _e: on_get())
        self.key.on_change(self._update_counters)

        tk.Frame(body, bg=th.SURFACE, height=p(14)).pack(fill="x")
        self._value_hint = field_label(body, theme, "Value")
        self.value = TextBox(body, theme, "Any text, up to 1 MB", lines=4,
                             on_submit=on_set)
        self.value.pack(fill="x")
        self.value.on_change(self._update_counters)

        row = tk.Frame(body, bg=th.SURFACE)
        row.pack(fill="x", pady=(p(14), 0))
        self.buttons = [FlatButton(row, theme, "Set", on_set, "primary"),
                        FlatButton(row, theme, "Get", on_get),
                        FlatButton(row, theme, "Delete", on_delete, "danger")]
        for i, button in enumerate(self.buttons):
            button.grid(row=0, column=i, sticky="ew", padx=(0 if i == 0 else p(8), 0))
            row.columnconfigure(i, weight=1, uniform="ops")
        tk.Label(body, text="Enter in Key runs Get  ·  Enter in Value runs Set",
                 bg=th.SURFACE, fg=th.TEXT_3, font=theme.font["small"],
                 anchor="w").pack(fill="x", pady=(p(8), 0))

        self._result = tk.Frame(body, bg=th.SURFACE)
        self._result.pack(fill="x", pady=(p(14), 0))
        self._update_counters()

    def _update_counters(self):
        key_size = rules.byte_len(self.key.get())
        value_size = rules.byte_len(self.value.get())
        key_over = key_size > rules.MAX_KEY_BYTES
        value_over = value_size > rules.MAX_VALUE_BYTES
        self._key_hint.configure(text=f"{key_size} / {rules.MAX_KEY_BYTES} B",
                                 fg=th.RED if key_over else th.TEXT_3)
        self._value_hint.configure(text=f"{rules.fmt_bytes(value_size)} / 1 MB",
                                   fg=th.RED if value_over else th.TEXT_3)
        self.key.set_invalid(key_over)
        self.value.set_invalid(value_over)

    def fill(self, key, value):
        self.key.set(key)
        self.value.set(value)

    def show_result(self, kind, title, detail=""):
        """Short outcome message under the buttons."""
        for child in self._result.winfo_children():
            child.destroy()
        t = self._theme
        tk.Label(self._result, text=title, bg=th.SURFACE, fg=_RESULT_COLORS[kind],
                 font=t.font["title"], anchor="w", justify="left",
                 wraplength=t.px(270)).pack(fill="x")
        if detail:
            if len(detail) > 400:
                detail = detail[:400] + "…"
            tk.Label(self._result, text=detail, bg=th.SURFACE, fg=th.TEXT_2,
                     font=t.font["mono_small"], anchor="w", justify="left",
                     wraplength=t.px(270)).pack(fill="x", pady=(t.px(3), 0))


class DataPanel(Card):
    """All live keys in a table, with a client-side filter."""

    PREVIEW_CHARS = 80

    def __init__(self, parent, theme, on_select, on_refresh):
        super().__init__(parent, theme, "Data")
        self._theme = theme
        self._rows = []
        self._on_select = on_select
        p, body = theme.px, self.body

        FlatButton(self.header, theme, "Refresh", on_refresh, "ghost",
                   small=True).pack(side="right")
        self._count = tk.Label(self.header, bg=th.SURFACE, fg=th.TEXT_3,
                               font=theme.font["small"])
        self._count.pack(side="right", padx=p(10))

        self.filter = InputBox(body, theme, "Filter keys")
        self.filter.pack(fill="x", pady=(0, p(12)))
        self.filter.on_change(self._render)

        table = tk.Frame(body, bg=th.SURFACE, highlightthickness=1,
                         highlightbackground=th.BORDER)
        table.pack(fill="both", expand=True)
        self.tree = ttk.Treeview(table, columns=("key", "value", "size"),
                                 show="headings", style="Data.Treeview",
                                 selectmode="browse")
        for col, title, width, anchor, stretch in (
                ("key", "KEY", 140, "w", False), ("value", "VALUE", 160, "w", True),
                ("size", "SIZE", 76, "e", False)):
            self.tree.heading(col, text=title, anchor=anchor)
            self.tree.column(col, width=p(width), minwidth=p(60), anchor=anchor,
                             stretch=stretch)
        scroll = ttk.Scrollbar(table, orient="vertical", command=self.tree.yview,
                               style="Thin.Vertical.TScrollbar")
        self.tree.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.tree.pack(side="left", fill="both", expand=True)
        for sequence in ("<ButtonRelease-1>", "<KeyRelease-Up>", "<KeyRelease-Down>"):
            self.tree.bind(sequence, self._user_selected)

        self._empty = tk.Label(table, bg=th.SURFACE, fg=th.TEXT_3,
                               font=theme.font["body"], justify="center")
        self._render()

    def set_rows(self, rows):
        """rows: list of (key, value) in display order."""
        self._rows = rows
        self._render()

    def _render(self):
        selected = self.selected_key()
        needle = self.filter.get().strip().lower()
        shown = [(k, v) for k, v in self._rows if needle in k.lower()]
        self.tree.delete(*self.tree.get_children())
        for key, value in shown:
            preview = value if len(value) <= self.PREVIEW_CHARS else \
                value[:self.PREVIEW_CHARS - 1] + "…"
            size = rules.fmt_bytes(rules.byte_len(value))
            self.tree.insert("", "end", iid=key, values=(key, preview, size))
        if selected and self.tree.exists(selected):
            self.tree.selection_set(selected)
        self._update_count(len(shown))
        self._update_empty_state(shown, needle)

    def _update_count(self, shown):
        total = len(self._rows)
        noun = "key" if total == 1 else "keys"
        text = f"{shown} of {total} {noun}" if shown != total else f"{total} {noun}"
        self._count.configure(text=text)

    def _update_empty_state(self, shown, needle):
        if shown:
            self._empty.place_forget()
            return
        if needle:
            text = f"No keys match “{needle}”"
        else:
            text = "No keys yet\nUse Set to add one."
        self._empty.configure(text=text)
        self._empty.place(relx=0.5, rely=0.45, anchor="center")

    def selected_key(self):
        sel = self.tree.selection()
        return sel[0] if sel else None

    def select(self, key):
        if self.tree.exists(key):
            self.tree.selection_set(key)
            self.tree.see(key)

    def _user_selected(self, event):
        """Load the row into the editor only when the USER picks it (click or
        arrow key). Re-selecting after a refresh must not overwrite text the
        user is still editing."""
        if event.type == tk.EventType.ButtonRelease and \
                self.tree.identify_region(event.x, event.y) != "cell":
            return
        key = self.selected_key()
        if key is not None:
            self._on_select(key, dict(self._rows).get(key, ""))


class StatsPanel(Card):
    """Storage metrics, dead-record ratio and the last recovery result."""

    def __init__(self, parent, theme):
        super().__init__(parent, theme, "Storage")
        self._theme = theme
        p, body = theme.px, self.body

        grid = tk.Frame(body, bg=th.SURFACE)
        grid.pack(fill="x")
        self._tiles = {}
        for i, (name, label) in enumerate((("live_keys", "Live keys"),
                                           ("log_bytes", "Log size"),
                                           ("log_records", "Records"),
                                           ("dead_records", "Dead records"))):
            tile = tk.Frame(grid, bg=th.SURFACE_2)
            tile.grid(row=i // 2, column=i % 2, sticky="nsew",
                      padx=(0 if i % 2 == 0 else p(8), 0),
                      pady=(0 if i < 2 else p(8), 0))
            grid.columnconfigure(i % 2, weight=1, uniform="tiles")
            tk.Label(tile, text=label, bg=th.SURFACE_2, fg=th.TEXT_3,
                     font=theme.font["small"], anchor="w").pack(
                         fill="x", padx=p(12), pady=(p(9), 0))
            value = tk.Label(tile, text="–", bg=th.SURFACE_2, fg=th.TEXT,
                             font=theme.font["metric"], anchor="w")
            value.pack(fill="x", padx=p(12), pady=(0, p(8)))
            self._tiles[name] = value

        ratio_row = tk.Frame(body, bg=th.SURFACE)
        ratio_row.pack(fill="x", pady=(p(16), p(6)))
        tk.Label(ratio_row, text="Dead space", bg=th.SURFACE, fg=th.TEXT_2,
                 font=theme.font["label"]).pack(side="left")
        self._ratio_text = tk.Label(ratio_row, bg=th.SURFACE, fg=th.TEXT_3,
                                    font=theme.font["small"])
        self._ratio_text.pack(side="right")
        self._ratio = RatioBar(body, theme)
        self._ratio.pack(fill="x")

        tk.Frame(body, bg=th.BORDER, height=1).pack(fill="x", pady=p(16))
        tk.Label(body, text="Last startup", bg=th.SURFACE, fg=th.TEXT_2,
                 font=theme.font["label"], anchor="w").pack(fill="x")
        self._recovery = tk.Frame(body, bg=th.SURFACE)
        self._recovery.pack(fill="x", pady=(p(6), 0))

    def set_stats(self, s):
        self._tiles["live_keys"].configure(text=f"{s['live_keys']:,}")
        self._tiles["log_bytes"].configure(text=rules.fmt_bytes(s["log_bytes"]))
        self._tiles["log_records"].configure(text=f"{s['log_records']:,}")
        self._tiles["dead_records"].configure(text=f"{s['dead_records']:,}")
        records = s["log_records"]
        ratio = s["dead_records"] / records if records else 0.0
        color = th.GREEN if ratio < 0.3 else th.AMBER if ratio < 0.6 else th.RED
        self._ratio.set(ratio, color)
        hint = "compact to reclaim" if ratio >= 0.3 else "healthy"
        self._ratio_text.configure(text=f"{ratio:.0%}  ·  {hint}")

    def set_recovery(self, rec):
        for child in self._recovery.winfo_children():
            child.destroy()
        t, p = self._theme, self._theme.px
        bad = rec.get("corruption", False)
        status = tk.Label(self._recovery, bg=th.SURFACE, anchor="w",
                          font=t.font["title"],
                          fg=th.AMBER if bad else th.GREEN,
                          text="Corruption found and repaired" if bad else "Clean start")
        status.pack(fill="x", pady=(0, p(4)))
        rows = [("Records replayed", f"{rec['records']:,}"),
                ("Time", f"{rec['ms']:.1f} ms")]
        if bad:
            rows.append(("Discarded", rules.fmt_bytes(rec["bytes_discarded"])))
        for label, value in rows:
            line = tk.Frame(self._recovery, bg=th.SURFACE)
            line.pack(fill="x")
            tk.Label(line, text=label, bg=th.SURFACE, fg=th.TEXT_3,
                     font=t.font["small"]).pack(side="left")
            tk.Label(line, text=value, bg=th.SURFACE, fg=th.TEXT_2,
                     font=t.font["mono_small"]).pack(side="right")
        if bad:
            tk.Label(self._recovery, text=rec["reason"], bg=th.SURFACE,
                     fg=th.TEXT_2, font=t.font["mono_small"], anchor="w",
                     justify="left", wraplength=p(260)).pack(fill="x", pady=(p(6), 0))


class MaintenancePanel(Page):
    """Compact, snapshot and restore."""

    def __init__(self, parent, theme, on_compact, on_snapshot, on_restore):
        super().__init__(parent, theme)
        self._theme = theme
        self.buttons = []
        for title, detail, text, command in (
                ("Compact log", "Keep only the latest value of each key.",
                 "Compact", on_compact),
                ("Snapshot", "Save a checksummed backup file.", "Save…", on_snapshot),
                ("Restore", "Replace all data from a backup.", "Restore…", on_restore)):
            self._row(title, detail, text, command)
        self._status = tk.Label(self.body, bg=th.SURFACE, fg=th.TEXT_3, anchor="w",
                                justify="left", font=theme.font["small"],
                                wraplength=theme.px(280))
        self._status.pack(fill="x", pady=(theme.px(4), 0))

    def _row(self, title, detail, text, command):
        t, p = self._theme, self._theme.px
        row = tk.Frame(self.body, bg=th.SURFACE)
        row.pack(fill="x", pady=(0, p(16)))
        button = FlatButton(row, t, text, command, small=True)
        button.pack(side="right")
        self.buttons.append(button)
        tk.Label(row, text=title, bg=th.SURFACE, fg=th.TEXT, anchor="w",
                 font=t.font["body"]).pack(fill="x")
        tk.Label(row, text=detail, bg=th.SURFACE, fg=th.TEXT_3, anchor="w",
                 font=t.font["small"]).pack(fill="x")

    def set_status(self, kind, text):
        self._status.configure(text=text, fg=_RESULT_COLORS[kind])


class CrashLabPanel(Page):
    """Kill the engine, damage the log on purpose, restart, watch recovery."""

    MODES = {
        "truncate": ("Truncate", "Bytes to cut from the end", "5"),
        "flip": ("Flip byte", "Byte offset to invert", "20"),
        "garbage": ("Garbage", "Random bytes to append", "16"),
    }

    def __init__(self, parent, theme, on_simulate):
        super().__init__(parent, theme)
        p, body = theme.px, self.body
        self._on_simulate = on_simulate
        tk.Label(body, text="Kills the engine without a clean shutdown, damages "
                 "the log on disk, then restarts it so you can watch recovery.",
                 bg=th.SURFACE, fg=th.TEXT_3, font=theme.font["small"],
                 justify="left", anchor="w", wraplength=p(280)).pack(fill="x")

        self.mode = Segmented(body, theme,
                              [(k, v[0]) for k, v in self.MODES.items()],
                              on_change=self._mode_changed)
        self.mode.pack(fill="x", pady=(p(12), p(12)))
        self._label = tk.Label(body, bg=th.SURFACE, fg=th.TEXT_2, anchor="w",
                               font=theme.font["label"])
        self._label.pack(fill="x", pady=(0, p(5)))
        self.amount = InputBox(body, theme, "", mono=True)
        self.amount.pack(fill="x")
        self.button = FlatButton(body, theme, "Simulate crash", self._simulate,
                                 "danger")
        self.button.pack(fill="x", pady=(p(12), 0))
        self._error = tk.Label(body, bg=th.SURFACE, fg=th.RED, anchor="w",
                               font=theme.font["small"])
        self._error.pack(fill="x", pady=(p(6), 0))
        self._mode_changed(self.mode.value)

    def _mode_changed(self, mode):
        self._label.configure(text=self.MODES[mode][1])
        self.amount.set(self.MODES[mode][2])
        self._error.configure(text="")

    def _simulate(self):
        text = self.amount.get().strip()
        if not text.isdigit():
            self._error.configure(text="Enter a whole number.")
            return
        self._error.configure(text="")
        self._on_simulate(self.mode.value, int(text))


class ConsolePanel(Card):
    """Timestamped log of every command sent and every reply received."""

    MAX_LINES = 2000
    STYLES = {  # kind: (marker, colour)
        "send": ("→", th.TEXT), "send_quiet": ("→", th.TEXT_3),
        "ok": ("←", th.GREEN), "ok_quiet": ("←", th.TEXT_3),
        "err": ("←", th.RED), "event": ("•", th.AMBER),
        "info": ("•", th.TEXT_2), "error": ("!", th.RED),
    }

    def __init__(self, parent, theme):
        super().__init__(parent, theme, "Activity")
        FlatButton(self.header, theme, "Clear", self.clear, "ghost",
                   small=True).pack(side="right")
        tk.Label(self.header, text="commands sent to cskv.exe and replies",
                 bg=th.SURFACE, fg=th.TEXT_3, font=theme.font["small"]).pack(
                     side="left", padx=theme.px(10))
        frame = tk.Frame(self.body, bg=th.SURFACE)
        frame.pack(fill="both", expand=True)
        self.text = tk.Text(frame, height=5, width=1, bd=0, wrap="none", bg=th.SURFACE,
                            fg=th.TEXT_2, font=theme.font["mono_small"],
                            highlightthickness=0, state="disabled",
                            selectbackground=th.SELECT, cursor="arrow")
        scroll = ttk.Scrollbar(frame, orient="vertical", command=self.text.yview,
                               style="Thin.Vertical.TScrollbar")
        self.text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.text.pack(side="left", fill="both", expand=True)
        self.text.tag_configure("time", foreground=th.TEXT_3)
        for kind, (_marker, color) in self.STYLES.items():
            self.text.tag_configure(kind, foreground=color)

    def log(self, kind, message):
        if len(message) > 240:
            message = message[:240] + "…"
        marker = self.STYLES[kind][0]
        self.text.configure(state="normal")
        self.text.insert("end", time.strftime("%H:%M:%S  "), "time")
        self.text.insert("end", f"{marker} {message}\n", kind)
        lines = int(self.text.index("end-1c").split(".")[0])
        if lines > self.MAX_LINES:
            self.text.delete("1.0", f"{lines - self.MAX_LINES}.0")
        self.text.configure(state="disabled")
        self.text.see("end")

    def clear(self):
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.configure(state="disabled")
