"""Small custom widgets.

Stock Tk buttons and entries cannot be styled flat on Windows, so buttons,
inputs and status elements are built from plain Labels / Frames / Canvases.
"""
import tkinter as tk

import theme as th

# variant: (background, text, hover, pressed). None = parent background.
_BUTTONS = {
    "primary": (th.ACCENT, th.ON_ACCENT, th.ACCENT_HOVER, th.ACCENT_PRESSED),
    "secondary": (th.SURFACE_3, th.TEXT, "#353b46", "#30353f"),
    "danger": ("#3b2125", th.RED, "#4a282c", "#432427"),
    "ghost": (None, th.TEXT_2, th.SURFACE_3, th.SURFACE_2),
}


class FlatButton(tk.Label):
    """Flat button with hover/pressed states, keyboard focus and disabling."""

    def __init__(self, parent, theme, text, command=None, variant="secondary",
                 small=False):
        bg, fg, hover, pressed = _BUTTONS[variant]
        self._parent_bg = parent.cget("bg")
        self._colors = (bg or self._parent_bg, fg, hover, pressed)
        self._ghost = bg is None
        self._command = command
        self._enabled = True
        self._inside = False
        super().__init__(
            parent, text=text, bg=self._colors[0], fg=fg,
            font=theme.font["button_small" if small else "button"],
            padx=theme.px(10 if small else 14), pady=theme.px(4 if small else 7),
            cursor="hand2", takefocus=1, bd=0, highlightthickness=1,
            highlightbackground=self._parent_bg, highlightcolor=th.ACCENT)
        self.bind("<Enter>", self._on_enter)
        self.bind("<Leave>", self._on_leave)
        self.bind("<ButtonPress-1>", self._on_press)
        self.bind("<ButtonRelease-1>", self._on_release)
        self.bind("<Return>", lambda _e: self.invoke())
        self.bind("<space>", lambda _e: self.invoke())

    def _paint(self, bg):
        if self._enabled:
            self.configure(bg=bg)

    def _on_enter(self, _e):
        self._inside = True
        self._paint(self._colors[2])

    def _on_leave(self, _e):
        self._inside = False
        self._paint(self._colors[0])

    def _on_press(self, _e):
        self._paint(self._colors[3])

    def _on_release(self, _e):
        self._paint(self._colors[2] if self._inside else self._colors[0])
        if self._inside:
            self.invoke()

    def invoke(self):
        if self._enabled and self._command:
            self._command()

    def set_enabled(self, enabled):
        self._enabled = enabled
        if enabled:
            bg = self._colors[2] if self._inside else self._colors[0]
            self.configure(bg=bg, fg=self._colors[1], cursor="hand2", takefocus=1)
        else:
            bg = self._parent_bg if self._ghost else th.SURFACE_2
            self.configure(bg=bg, fg=th.TEXT_3, cursor="arrow", takefocus=0)


class Card(tk.Frame):
    """Bordered panel with a title row (self.header) and a body (self.body)."""

    def __init__(self, parent, theme, title):
        super().__init__(parent, bg=th.SURFACE, highlightthickness=1,
                         highlightbackground=th.BORDER)
        p = theme.px
        self.header = tk.Frame(self, bg=th.SURFACE)
        self.header.pack(fill="x", padx=p(16), pady=(p(13), p(10)))
        tk.Label(self.header, text=title, bg=th.SURFACE, fg=th.TEXT,
                 font=theme.font["title"]).pack(side="left")
        self.body = tk.Frame(self, bg=th.SURFACE)
        self.body.pack(fill="both", expand=True, padx=p(16), pady=(0, p(16)))


class Page(tk.Frame):
    """Plain content area inside a TabbedCard (self.body is the frame itself)."""

    def __init__(self, parent, theme):
        super().__init__(parent, bg=th.SURFACE)
        self.body = self


class TabbedCard(tk.Frame):
    """Card with an underlined tab row. Pages are stacked in one grid cell so
    switching tabs never changes the card's size."""

    def __init__(self, parent, theme):
        super().__init__(parent, bg=th.SURFACE, highlightthickness=1,
                         highlightbackground=th.BORDER)
        self._theme = theme
        self._tabs = {}            # id -> (label, underline, page)
        self._row = tk.Frame(self, bg=th.SURFACE)
        self._row.pack(fill="x", padx=theme.px(8))
        tk.Frame(self, bg=th.BORDER, height=1).pack(fill="x")
        self.pages = tk.Frame(self, bg=th.SURFACE)
        self.pages.pack(fill="both", expand=True, padx=theme.px(16),
                        pady=theme.px(16))
        self.pages.rowconfigure(0, weight=1)
        self.pages.columnconfigure(0, weight=1)
        self.current = None

    def add(self, tab_id, title, page):
        p = self._theme.px
        cell = tk.Frame(self._row, bg=th.SURFACE)
        cell.pack(side="left")
        label = tk.Label(cell, text=title, bg=th.SURFACE, cursor="hand2",
                         font=self._theme.font["title"], padx=p(10), pady=p(11))
        label.pack()
        underline = tk.Frame(cell, height=2, bg=th.SURFACE)
        underline.pack(fill="x", padx=p(8))
        label.bind("<Button-1>", lambda _e: self.select(tab_id))
        page.grid(row=0, column=0, sticky="nsew")
        self._tabs[tab_id] = (label, underline, page)
        self.select(self.current or tab_id)     # repaint every tab label

    def select(self, tab_id):
        self.current = tab_id
        for tid, (label, underline, page) in self._tabs.items():
            active = tid == tab_id
            label.configure(fg=th.TEXT if active else th.TEXT_3)
            underline.configure(bg=th.ACCENT if active else th.SURFACE)
            if active:
                page.tkraise()


def field_label(parent, theme, text):
    """Small label above an input, with a right-aligned hint/counter label.
    Returns the hint label."""
    row = tk.Frame(parent, bg=parent.cget("bg"))
    row.pack(fill="x", pady=(0, theme.px(5)))
    tk.Label(row, text=text, bg=row.cget("bg"), fg=th.TEXT_2,
             font=theme.font["label"]).pack(side="left")
    hint = tk.Label(row, text="", bg=row.cget("bg"), fg=th.TEXT_3,
                    font=theme.font["small"])
    hint.pack(side="right")
    return hint


class _InputFrame(tk.Frame):
    """Shared border/placeholder behaviour for single and multi-line inputs."""

    def __init__(self, parent, theme):
        super().__init__(parent, bg=th.SURFACE_2, highlightthickness=1,
                         highlightbackground=th.BORDER)
        self._focused = False
        self._invalid = False

    def _paint_border(self):
        color = th.ACCENT if self._focused else (th.RED if self._invalid else th.BORDER)
        self.configure(highlightbackground=color)

    def _focus_in(self, _e):
        self._focused = True
        self._paint_border()

    def _focus_out(self, _e):
        self._focused = False
        self._paint_border()

    def set_invalid(self, invalid):
        self._invalid = invalid
        self._paint_border()

    def _make_placeholder(self, theme, text, widget, **place):
        label = tk.Label(self, text=text, bg=th.SURFACE_2, fg=th.TEXT_3,
                         font=theme.font["body"], cursor="xterm")
        label.place(in_=widget, **place)
        label.bind("<Button-1>", lambda _e: widget.focus_set())
        widget.bind("<FocusIn>", self._focus_in, add="+")
        widget.bind("<FocusOut>", self._focus_out, add="+")
        return label


class InputBox(_InputFrame):
    """Single-line input with a placeholder and an accent focus border."""

    def __init__(self, parent, theme, placeholder="", mono=False, width=10):
        super().__init__(parent, theme)
        self.var = tk.StringVar()
        self.entry = tk.Entry(
            self, textvariable=self.var, bd=0, relief="flat", width=width,
            bg=th.SURFACE_2, fg=th.TEXT, insertbackground=th.TEXT,
            selectbackground=th.SELECT, selectforeground=th.TEXT,
            highlightthickness=0, font=theme.font["mono" if mono else "body"])
        self.entry.pack(fill="x", padx=theme.px(10), pady=theme.px(7))
        self._ph = self._make_placeholder(theme, placeholder, self.entry,
                                          x=0, rely=0.5, anchor="w")
        self.var.trace_add("write", lambda *_: self._sync_placeholder())

    def _sync_placeholder(self):
        if self.var.get():
            self._ph.place_forget()
        else:
            self._ph.place(in_=self.entry, x=0, rely=0.5, anchor="w")

    def get(self):
        return self.var.get()

    def set(self, value):
        self.var.set(value)

    def on_change(self, callback):
        self.var.trace_add("write", lambda *_: callback())

    def bind_key(self, sequence, callback):
        self.entry.bind(sequence, callback)

    def focus(self):
        self.entry.focus_set()


class TextBox(_InputFrame):
    """Multi-line, wrapping input. Enter runs `on_submit` instead of adding a
    line break (values cannot contain line breaks)."""

    def __init__(self, parent, theme, placeholder="", lines=4, on_submit=None):
        super().__init__(parent, theme)
        p = theme.px
        self.text = tk.Text(
            self, height=lines, width=1, wrap="char", bd=0, undo=True,
            bg=th.SURFACE_2, fg=th.TEXT, insertbackground=th.TEXT,
            selectbackground=th.SELECT, selectforeground=th.TEXT,
            highlightthickness=0, padx=p(10), pady=p(8), font=theme.font["body"])
        self.text.pack(fill="both", expand=True)
        # place(in_=Text) measures from inside the Text's padding already.
        self._ph_place = {"x": 0, "y": 0}
        self._ph = self._make_placeholder(theme, placeholder, self.text,
                                          **self._ph_place)
        self._callbacks = []
        self.text.bind("<<Modified>>", self._on_modified)
        if on_submit:
            self.text.bind("<Return>", lambda _e: (on_submit(), "break")[1])

    def _on_modified(self, _e):
        if not self.text.edit_modified():
            return                      # our own reset below re-fires the event
        self.text.edit_modified(False)
        if self.get():
            self._ph.place_forget()
        else:
            self._ph.place(in_=self.text, **self._ph_place)
        for cb in self._callbacks:
            cb()

    def get(self):
        return self.text.get("1.0", "end-1c")

    def set(self, value):
        self.text.delete("1.0", "end")
        self.text.insert("1.0", value)

    def on_change(self, callback):
        self._callbacks.append(callback)


class Segmented(tk.Frame):
    """A row of mutually exclusive options (like a segmented control)."""

    def __init__(self, parent, theme, options, on_change=None):
        super().__init__(parent, bg=th.SURFACE_2, highlightthickness=1,
                         highlightbackground=th.BORDER)
        self._labels = {}
        self._on_change = on_change
        self.value = options[0][0]
        for i, (value, text) in enumerate(options):
            label = tk.Label(self, text=text, font=theme.font["button_small"],
                             pady=theme.px(6), cursor="hand2", bd=0)
            label.grid(row=0, column=i, sticky="nsew", padx=1, pady=1)
            self.columnconfigure(i, weight=1, uniform="seg")
            label.bind("<Button-1>", lambda _e, v=value: self.select(v))
            self._labels[value] = label
        self._paint()

    def select(self, value):
        if value != self.value:
            self.value = value
            self._paint()
            if self._on_change:
                self._on_change(value)

    def _paint(self):
        for value, label in self._labels.items():
            active = value == self.value
            label.configure(bg=th.SURFACE_3 if active else th.SURFACE_2,
                            fg=th.TEXT if active else th.TEXT_3)


class StatusPill(tk.Frame):
    """Coloured dot plus text, e.g. "Running - pid 1234"."""

    COLORS = {"running": th.GREEN, "starting": th.AMBER,
              "stopped": th.TEXT_3, "error": th.RED}

    def __init__(self, parent, theme):
        bg = parent.cget("bg")
        super().__init__(parent, bg=bg)
        size = theme.px(8)
        self._dot = tk.Canvas(self, width=size, height=size, bg=bg,
                              highlightthickness=0)
        self._oval = self._dot.create_oval(0, 0, size - 1, size - 1, width=0)
        self._dot.pack(side="left", padx=(0, theme.px(7)))
        self._label = tk.Label(self, bg=bg, fg=th.TEXT_2, font=theme.font["small"])
        self._label.pack(side="left")

    def set(self, state, text):
        self._dot.itemconfigure(self._oval, fill=self.COLORS[state])
        self._label.configure(text=text)


class RatioBar(tk.Canvas):
    """Thin horizontal bar showing a fraction between 0 and 1."""

    def __init__(self, parent, theme):
        super().__init__(parent, height=theme.px(6), bg=parent.cget("bg"),
                         highlightthickness=0)
        self._fraction = 0.0
        self._color = th.GREEN
        self.bind("<Configure>", lambda _e: self._draw())

    def set(self, fraction, color):
        self._fraction = max(0.0, min(1.0, fraction))
        self._color = color
        self._draw()

    def _draw(self):
        self.delete("all")
        w, h = self.winfo_width(), self.winfo_height()
        self.create_rectangle(0, 0, w, h, fill=th.SURFACE_3, width=0)
        if self._fraction > 0:
            self.create_rectangle(0, 0, max(2, w * self._fraction), h,
                                  fill=self._color, width=0)


class Banner(tk.Frame):
    """Full-width message strip: success / info / warning / error."""

    STYLES = {"success": (th.GREEN_SOFT, th.GREEN), "info": (th.ACCENT_SOFT, th.ACCENT),
              "warning": (th.AMBER_SOFT, th.AMBER), "error": (th.RED_SOFT, th.RED)}

    def __init__(self, parent, theme):
        super().__init__(parent, bg=parent.cget("bg"))
        self._theme = theme
        self._hide_job = None
        self._inner = None

    def show(self, kind, title, detail="", action=None, auto_hide_ms=None):
        """action: optional (button text, callback)."""
        self._cancel_hide()
        if self._inner is not None:
            self._inner.destroy()
        soft, strong = self.STYLES[kind]
        t, p = self._theme, self._theme.px
        inner = self._inner = tk.Frame(self, bg=soft)
        inner.pack(fill="x")
        tk.Frame(inner, bg=strong, width=p(3)).pack(side="left", fill="y")
        text = tk.Frame(inner, bg=soft)
        text.pack(side="left", fill="x", expand=True, padx=p(14), pady=p(8))
        tk.Label(text, text=title, bg=soft, fg=strong,
                 font=t.font["title"]).pack(side="left")
        if detail:
            tk.Label(text, text=detail, bg=soft, fg=th.TEXT_2, anchor="w",
                     font=t.font["small"]).pack(side="left", padx=(p(12), 0))
        close = tk.Label(inner, text="×", bg=soft, fg=th.TEXT_3,
                         font=t.font["brand"], cursor="hand2", padx=p(12))
        close.pack(side="right")
        close.bind("<Button-1>", lambda _e: self.hide())
        if action:
            FlatButton(inner, t, action[0], action[1], "secondary",
                       small=True).pack(side="right", pady=p(8))
        self.grid()
        if auto_hide_ms:
            self._hide_job = self.after(auto_hide_ms, self.hide)

    def hide(self):
        self._cancel_hide()
        self.grid_remove()

    def _cancel_hide(self):
        if self._hide_job is not None:
            self.after_cancel(self._hide_job)
            self._hide_job = None
