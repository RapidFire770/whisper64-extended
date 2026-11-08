from pathlib import Path

EXPLAIN = {
    "duplicate label": "This label name is already used. Rename one of them in the .bas file.",
    "unknown label": "This jump names a label the file does not define. Add the label or correct the name.",
    "unknown line": "This jump uses a line number the file does not contain.",
    "label character": "W64LABEL only takes letters and digits in a label. Remove the other character.",
    "line number": "A line number must be from 0 through 63999.",
    "duplicate line": "Two lines use this number. BASIC keeps one line per number.",
    "character": "The editor keeps characters from space through tilde. Replace this character in the .bas file.",
    "comment word": "A word in this comment is longer than a REM line. Break the word in the .bas file.",
    "expression piece": "One piece between + signs is longer than 79 characters. Shorten that piece in the .bas file.",
    "line length": "This statement is longer than 79 characters and has no colon or + to split on. Shorten it in the .bas file.",
}


class Block:
    def __init__(self, line_no, phrase, detail, original, replacement=None):
        self.line_no = line_no
        self.phrase = phrase
        self.detail = detail
        self.original = original
        self.replacement = replacement


class Session:
    def __init__(self):
        self.error = None
        self.source = []
        self.pending = []
        self.auto = {}
        self.given = {}
        self.items = []
        self.renames = []
        self.uses = {}


class Outcome:
    def __init__(self):
        self.unresolved = []
        self.warnings = []
        self.lines = None


def split_lines(text):
    if text == "":
        return []
    lines = []
    buf = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "\r":
            lines.append("".join(buf))
            buf = []
            i += 2 if i + 1 < len(text) and text[i + 1] == "\n" else 1
            continue
        if ch == "\n":
            lines.append("".join(buf))
            buf = []
            i += 1
            continue
        buf.append(ch)
        i += 1
    if buf:
        lines.append("".join(buf))
    return lines


def line_number(text):
    """None when the line is not numbered. ('bad',) when the number exceeds 63999.
    ('ok', value) otherwise."""
    i = 0
    while i < len(text) and text[i] == " ":
        i += 1
    if i >= len(text) or not text[i].isdigit():
        return None
    value = 0
    over = False
    while i < len(text) and text[i].isdigit():
        digit = ord(text[i]) - ord("0")
        if not over:
            if value > 63999 // 10 or (value == 63999 // 10 and digit > 63999 % 10):
                over = True
            else:
                value = value * 10 + digit
        i += 1
    if i < len(text) and text[i] != " ":
        return None
    if over:
        return ("bad",)
    return ("ok", value)


def _character_block(line_no, text):
    for ch in text:
        if ord(ch) < 32 or ord(ch) > 126:
            return Block(line_no, "character", EXPLAIN["character"], text)
    return None


def load(text):
    session = Session()
    session.source = split_lines(text)
    seen = {}
    for line_no, line in enumerate(session.source, 1):
        block = _character_block(line_no, line)
        if block:
            session.items.append(block)
            continue
        numbered = line_number(line)
        if numbered is None:
            continue
        if numbered[0] == "bad":
            session.items.append(Block(line_no, "line number", EXPLAIN["line number"], line))
            continue
        value = numbered[1]
        if value in seen:
            session.items.append(Block(line_no, "duplicate line", EXPLAIN["duplicate line"], line))
        else:
            seen[value] = line_no
    return session


def load_path(path):
    session = Session()
    try:
        raw = Path(path).read_bytes()
    except OSError as err:
        session.error = str(err)
        return session
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        session.error = "not UTF-8"
        return session
    return load(text)
