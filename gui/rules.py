"""Input rules and formatting shared by the GUI.

The checks mirror cli/commands.c so bad input is caught before anything is
sent to the engine (CLAUDE.md 9.3).
"""
MAX_KEY_BYTES = 256
MAX_VALUE_BYTES = 1024 * 1024


def byte_len(text):
    return len(text.encode("utf-8"))


def fmt_bytes(n):
    if n < 1024:
        return f"{n} B"
    if n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def check_key(key):
    """Returns an error message, or None if the key is valid."""
    if not key:
        return "Enter a key."
    if any(ch.isspace() for ch in key):
        return "Keys can't contain spaces."
    if any(not 0x21 <= ord(ch) <= 0x7E for ch in key):
        return "Keys can only use printable ASCII (letters, digits, symbols)."
    if byte_len(key) > MAX_KEY_BYTES:
        return f"Key is too long: {byte_len(key)} of max {MAX_KEY_BYTES} bytes."
    return None


def check_value(value):
    """Returns an error message, or None if the value is valid."""
    if not value:
        return "Enter a value."
    if "\n" in value or "\r" in value:
        return "Values can't contain line breaks."
    if any(ord(ch) < 0x20 and ch != "\t" for ch in value):
        return "Values can't contain control characters."
    if value[0] in " \t":
        return "Values can't start with a space."
    size = byte_len(value)
    if size > MAX_VALUE_BYTES:
        return f"Value is too large: {fmt_bytes(size)} of max 1 MB."
    return None
