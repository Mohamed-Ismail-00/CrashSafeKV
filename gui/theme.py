"""Colours, fonts and ttk styles.

One calm graphite palette with a single blue accent. Green / amber / red are
reserved for meaning (healthy, warning, failure) and never used as decoration.
"""
from tkinter import font as tkfont
from tkinter import ttk

# Surfaces, darkest to lightest.
BG = "#14161b"
SURFACE = "#1b1e24"
SURFACE_2 = "#22262e"
SURFACE_3 = "#2b3039"
BORDER = "#2a2f37"

# Text, strongest to faintest.
TEXT = "#e4e7eb"
TEXT_2 = "#a3aab5"
TEXT_3 = "#6d7581"

ACCENT = "#7aa7f7"
ACCENT_HOVER = "#93b8fa"
ACCENT_PRESSED = "#6896e6"
ON_ACCENT = "#0e1726"
SELECT = "#25324a"

GREEN = "#5cc290"
AMBER = "#e2a54d"
RED = "#ee7070"
GREEN_SOFT = "#17271f"
AMBER_SOFT = "#2b2318"
RED_SOFT = "#2d1a1d"
ACCENT_SOFT = "#1a2436"


def _first_available(families, *names):
    return next((n for n in names if n in families), names[-1])


class Theme:
    """Fonts and pixel scaling for the current display."""

    def __init__(self, root):
        # Font sizes are in points and scale automatically; paddings are in
        # pixels, so they are multiplied by the display scale (125%, 150% ...).
        self.scale = max(1.0, root.winfo_fpixels("1i") / 96.0)
        families = set(tkfont.families(root))
        ui = _first_available(families, "Segoe UI", "Arial")
        semi = _first_available(families, "Segoe UI Semibold", ui)
        mono = _first_available(families, "Cascadia Mono", "Cascadia Code",
                                "Consolas", "Courier New")
        self.font = {
            "body": (ui, 10),
            "small": (ui, 9),
            "label": (semi, 9),
            "title": (semi, 10),
            "brand": (semi, 13),
            "metric": (semi, 18),
            "button": (semi, 9),
            "button_small": (ui, 9),
            "mono": (mono, 10),
            "mono_small": (mono, 9),
        }
        self._configure_ttk(root)

    def px(self, n):
        return int(round(n * self.scale))

    def _configure_ttk(self, root):
        style = ttk.Style(root)
        style.theme_use("clam")

        style.configure("Data.Treeview", background=SURFACE,
                        fieldbackground=SURFACE, foreground=TEXT,
                        bordercolor=SURFACE, borderwidth=0,
                        rowheight=self.px(30), font=self.font["mono"])
        style.map("Data.Treeview",
                  background=[("selected", SELECT)],
                  foreground=[("selected", TEXT)])
        style.layout("Data.Treeview",
                     [("Treeview.treearea", {"sticky": "nswe"})])
        style.configure("Data.Treeview.Heading", background=SURFACE_2,
                        foreground=TEXT_3, font=self.font["label"],
                        relief="flat", borderwidth=0,
                        padding=(self.px(10), self.px(7)))
        style.map("Data.Treeview.Heading",
                  background=[("active", SURFACE_2)],
                  foreground=[("active", TEXT_2)])
        # Drop the dotted focus rectangle drawn around the focused row.
        try:
            style.layout("Item", [("Treeitem.padding", {"sticky": "nswe", "children": [
                ("Treeitem.indicator", {"side": "left", "sticky": ""}),
                ("Treeitem.text", {"side": "left", "sticky": ""})]})])
        except Exception:      # older Tk builds name elements differently
            pass

        style.layout("Thin.Vertical.TScrollbar", [
            ("Vertical.Scrollbar.trough", {"sticky": "ns", "children": [
                ("Vertical.Scrollbar.thumb", {"expand": "1", "sticky": "nswe"})]})])
        style.configure("Thin.Vertical.TScrollbar", troughcolor=SURFACE,
                        background=SURFACE_3, bordercolor=SURFACE,
                        lightcolor=SURFACE_3, darkcolor=SURFACE_3,
                        arrowsize=self.px(8), gripcount=0)
        style.map("Thin.Vertical.TScrollbar",
                  background=[("active", "#3a404b"), ("pressed", "#3a404b")])
