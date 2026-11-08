from pathlib import Path

from bas2seq.scan import TOKEN_KEYWORDS, is_letter, is_name_char, mask, word_at

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
        self.ready = False


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


def take_label(text):
    """Return the label name when the line starts with a legal or too-long label.
    None when the line does not start with name characters and a colon."""
    i = 0
    while i < len(text) and text[i] == " ":
        i += 1
    if i >= len(text) or not is_letter(text[i]):
        return None
    start = i
    while i < len(text) and is_name_char(text[i]):
        i += 1
    if i >= len(text) or text[i] != ":":
        return None
    return text[start:i]


def illegal_label(text):
    """True when a leading token reaches a colon through a character a label cannot hold."""
    i = 0
    while i < len(text) and text[i] == " ":
        i += 1
    if i >= len(text) or not is_letter(text[i]):
        return False
    saw_bad = False
    while i < len(text) and text[i] not in " :\"":
        if not is_name_char(text[i]):
            saw_bad = True
        i += 1
    return saw_bad and i < len(text) and text[i] == ":"


def _plan_renames(names):
    """names is first-seen order, duplicates already removed. Returns (auto, pending)."""
    occupied = {name for name in names if len(name) <= 11}
    auto = {}
    pending = []
    groups = {}
    order = []
    for name in names:
        if len(name) <= 11:
            continue
        candidate = None
        if name.startswith("label") and len(name) > 5 and is_letter(name[5]):
            rest = name[5:]
            if 1 <= len(rest) <= 11 and all(is_name_char(ch) for ch in rest):
                candidate = rest
        if candidate is None:
            pending.append(name)
            continue
        if candidate not in groups:
            order.append(candidate)
        groups.setdefault(candidate, []).append(name)
    for candidate in order:
        olds = groups[candidate]
        if len(olds) > 1 or candidate in occupied:
            pending.extend(olds)
        else:
            auto[olds[0]] = candidate
            occupied.add(candidate)
    return auto, pending


def mapping(session):
    names = dict(session.auto)
    names.update(session.given)
    return names


def supply(session, old, new):
    if old not in session.pending:
        return "unknown name"
    if new == "":
        return "empty"
    if not new or not is_letter(new[0]) or any(not is_name_char(ch) for ch in new):
        return "label character"
    if len(new) > 11:
        return "too long"
    taken = {name for name in session.auto.values()}
    taken.update(session.given.values())
    taken.update(name for name in _defined_names(session.source) if len(name) <= 11)
    if new in taken:
        return "already used"
    session.given[old] = new
    session.pending.remove(old)
    session.ready = False
    if old not in dict(session.renames):
        session.renames.append((old, new))
    session.renames = [(src, mapping(session)[src]) for src, _dst in session.renames]
    return None


def _defined_names(source):
    found = []
    seen = set()
    for line in source:
        if _character_block(0, line):
            continue
        name = take_label(line)
        if name and name not in seen:
            seen.add(name)
            found.append(name)
    return found


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
    defined = []
    seen_labels = set()
    for line_no, line in enumerate(session.source, 1):
        if any(item.line_no == line_no and item.phrase == "character" for item in session.items):
            continue
        if illegal_label(line):
            session.items.append(Block(line_no, "label character", EXPLAIN["label character"], line))
            continue
        name = take_label(line)
        if not name:
            continue
        if name in seen_labels:
            session.items.append(Block(line_no, "duplicate label", EXPLAIN["duplicate label"], line))
            continue
        seen_labels.add(name)
        defined.append(name)
    session.auto, session.pending = _plan_renames(defined)
    session.renames = [(name, session.auto[name]) for name in defined if name in session.auto]
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


def _join_go(text):
    regions = mask(text)
    out = []
    notes = []
    i = 0
    while i < len(text):
        if regions[i] != "code":
            out.append(text[i])
            i += 1
            continue
        matched = _go_phrase(text, i)
        if matched:
            size, repl, note = matched
            out.append(repl)
            if note not in notes:
                notes.append(note)
            i += size
            continue
        out.append(text[i])
        i += 1
    return "".join(out), notes


def _go_phrase(text, index):
    if index > 0 and is_letter(text[index - 1]):
        return None
    if not text.startswith("GO", index):
        return None
    if index + 2 < len(text) and is_letter(text[index + 2]):
        return None
    j = index + 2
    if j >= len(text) or text[j] != " ":
        return None
    while j < len(text) and text[j] == " ":
        j += 1
    for second, repl, note in (("TO", "GOTO", "GO TO joined"), ("SUB", "GOSUB", "GO SUB joined")):
        if text.startswith(second, j):
            end = j + len(second)
            if end < len(text) and is_letter(text[end]):
                continue
            return end - index, repl, note
    return None


def _collapse(text):
    regions = mask(text)
    out = []
    for i, ch in enumerate(text):
        if ch == " " and regions[i] == "code" and out and out[-1] == " ":
            continue
        out.append(ch)
    return "".join(out)


def _drop_comment(text):
    regions = mask(text)
    rem_at = None
    for i in range(len(text)):
        if regions[i] == "code" and word_at(text, i, "REM"):
            rem_at = i
            break
    if rem_at is None:
        return None
    head = text[:rem_at].rstrip(" ")
    if head == "":
        return None
    if head.endswith(":"):
        head = head[:-1].rstrip(" ")
    if len(head) <= 79:
        return head
    return None


def _shorten_print(text):
    regions = mask(text)
    out = []
    changed = False
    i = 0
    while i < len(text):
        if regions[i] == "code" and word_at(text, i, "PRINT"):
            nxt = i + 5
            if nxt < len(text) and text[nxt] == "#":
                out.append(text[i])
                i += 1
                continue
            out.append("?")
            changed = True
            i += 5
            continue
        out.append(text[i])
        i += 1
    return "".join(out), changed


def _propose_early(text):
    """Return (lines, reason) when join, collapse, comment drop, or PRINT makes the line fit.
    Return None when the line still needs a later step. Return ('same',) when nothing changes."""
    joined, notes = _join_go(text)
    if len(joined) <= 79:
        if notes:
            return [joined], ", ".join(notes)
        return ("same",)
    work = joined
    collapsed = _collapse(work)
    if collapsed != work:
        work = collapsed
        notes.append("spaces collapsed")
    if len(work) <= 79:
        return [work], ", ".join(notes)
    dropped = _drop_comment(work)
    if dropped is not None:
        notes.append("comment removed")
        return [dropped], ", ".join(notes)
    shortened, changed = _shorten_print(work)
    if changed:
        notes.append("PRINT shortened")
        work = shortened
    if len(work) <= 79:
        return [work], ", ".join(notes)
    return None


def _split_colons(text):
    regions = mask(text)
    label_colon = None
    name = take_label(text)
    if name is not None:
        lead = 0
        while lead < len(text) and text[lead] == " ":
            lead += 1
        label_colon = lead + len(name)
    parts = []
    buf = []
    drop_space = False
    for i, ch in enumerate(text):
        if drop_space:
            drop_space = False
            if ch == " ":
                continue
        if ch == ":" and regions[i] == "code":
            if label_colon is not None and i == label_colon:
                buf.append(ch)
                continue
            piece = "".join(buf)
            if piece.strip() != "":
                parts.append(piece)
            buf = []
            drop_space = True
            continue
        buf.append(ch)
    piece = "".join(buf)
    if piece.strip() != "":
        parts.append(piece)
    return parts if parts else [text]


def _wrap_rem(piece):
    regions = mask(piece)
    start = 0
    while start < len(piece) and piece[start] == " ":
        start += 1
    if not word_at(piece, start, "REM"):
        return None
    body = piece[start + 3:]
    if body.startswith(" "):
        body = body[1:]
    lines = []
    while True:
        if len(body) <= 75:
            lines.append("REM" if body == "" else "REM " + body)
            break
        window = body[:75]
        cut = window.rfind(" ")
        if cut <= 0:
            return "comment word"
        lines.append("REM " + body[:cut])
        body = body[cut + 1:]
    if any(len(line) > 79 for line in lines):
        return "comment word"
    return lines


def _split_assign(piece):
    label = ""
    rest = piece
    name = take_label(piece)
    if name and len(name) <= 11:
        mark = piece.find(":")
        rest = piece[mark + 1:]
        if rest.startswith(" "):
            rest = rest[1:]
        label = name + ": "
    stripped = rest.lstrip()
    i = 0
    if not stripped or not is_letter(stripped[0]):
        return None
    while i < len(stripped) and (is_name_char(stripped[i]) or stripped[i] in "$%"):
        if stripped[i] in "$%":
            i += 1
            break
        i += 1
    dest = stripped[:i]
    rest = stripped[i:].lstrip()
    if not rest.startswith("="):
        return None
    expr = rest[1:].strip()
    regions = mask(expr)
    if not any(ch == "+" and regions[idx] == "code" for idx, ch in enumerate(expr)):
        return None
    operands = []
    buf = []
    regions = mask(expr)
    for idx, ch in enumerate(expr):
        if ch == "+" and regions[idx] == "code":
            operands.append("".join(buf).strip())
            buf = []
            continue
        buf.append(ch)
    operands.append("".join(buf).strip())
    if any(op == "" for op in operands):
        return None

    def pack(ops, budget):
        chosen = []
        used = 0
        for op in ops:
            extra = len(op) if not chosen else len(" + ") + len(op)
            if used + extra > budget:
                break
            if not chosen and len(op) > budget:
                return None
            chosen.append(op)
            used += extra
        return chosen

    lines = []
    left = operands
    first_budget = 79 - len(label + dest + " = ")
    nxt = pack(left, first_budget)
    if not nxt:
        return "expression piece"
    lines.append(label + dest + " = " + " + ".join(nxt))
    left = left[len(nxt):]
    cont = dest + " = " + dest + " + "
    while left:
        nxt = pack(left, 79 - len(cont))
        if not nxt:
            return "expression piece"
        lines.append(cont + " + ".join(nxt))
        left = left[len(nxt):]
    if any(len(line) > 79 for line in lines):
        return "expression piece"
    return lines


def _finish_pieces(pieces):
    """Return (lines, notes) or a blocking phrase."""
    notes = []
    out = []
    for piece in pieces:
        if len(piece) <= 79:
            out.append(piece)
            continue
        wrapped = _wrap_rem(piece)
        if wrapped == "comment word":
            return "comment word"
        if isinstance(wrapped, list):
            notes.append("comment wrapped")
            out.extend(wrapped)
            continue
        assigned = _split_assign(piece)
        if assigned == "expression piece":
            return "expression piece"
        if isinstance(assigned, list):
            notes.append("assignment split")
            out.extend(assigned)
            continue
        return "line length"
    return out, notes


def _letter_keyword(text, index):
    for keyword in TOKEN_KEYWORDS:
        if not any(ch.isalpha() for ch in keyword):
            continue
        if word_at(text, index, keyword):
            return keyword
    return None


def _final_labels(session):
    found = set()
    for line in session.source:
        if illegal_label(line):
            continue
        name = take_label(line)
        if name:
            found.add(mapping(session).get(name, name))
    return found


def _line_numbers(session):
    found = set()
    for line in session.source:
        numbered = line_number(line)
        if numbered and numbered[0] == "ok":
            found.add(numbered[1])
    return found


def _read_targets(session, line, index, out, names, finals, numbers, allow_keyword):
    while True:
        while index < len(line) and line[index] == " ":
            out.append(line[index])
            index += 1
        if index >= len(line):
            return index, None, set()
        used = set()
        if line[index].isdigit():
            end = index
            while end < len(line) and line[end].isdigit():
                end += 1
            if int(line[index:end]) not in numbers:
                return index, "unknown line", set()
            out.append(line[index:end])
            index = end
        elif is_letter(line[index]):
            if allow_keyword and _letter_keyword(line, index):
                return index, None, set()
            end = index
            while end < len(line) and is_name_char(line[end]):
                end += 1
            word = line[index:end]
            nxt = end
            while nxt < len(line) and line[nxt] == " ":
                nxt += 1
            if allow_keyword and nxt < len(line) and line[nxt] == "=":
                return index, None, set()
            final = names.get(word, word)
            if final not in finals:
                return index, "unknown label", set()
            out.append(final)
            if word in names:
                used.add(word)
            index = end
        else:
            return index, None, set()
        nxt = index
        while nxt < len(line) and line[nxt] == " ":
            nxt += 1
        if nxt < len(line) and line[nxt] == ",":
            while index < nxt:
                out.append(line[index])
                index += 1
            out.append(",")
            index = nxt + 1
            allow_keyword = False
            continue
        return index, None, used


def _rewrite_line(session, line):
    names = mapping(session)
    finals = _final_labels(session)
    numbers = _line_numbers(session)
    regions = mask(line)
    out = []
    used = set()
    index = 0
    defined = take_label(line)
    if defined and defined in names:
        while index < len(line) and line[index] == " ":
            out.append(line[index])
            index += 1
        out.append(names[defined])
        index += len(defined)
        used.add(defined)
    while index < len(line):
        if regions[index] != "code":
            out.append(line[index])
            index += 1
            continue
        keyword = None
        for candidate in ("GOTO", "GOSUB", "THEN", "ELSE"):
            if word_at(line, index, candidate):
                keyword = candidate
                break
        if not keyword:
            out.append(line[index])
            index += 1
            continue
        out.append(keyword)
        index += len(keyword)
        allow = keyword in ("THEN", "ELSE")
        if allow:
            nxt = index
            while nxt < len(line) and line[nxt] == " ":
                nxt += 1
            if word_at(line, nxt, "GOTO") or word_at(line, nxt, "GOSUB"):
                while index < nxt:
                    out.append(line[index])
                    index += 1
                allow = False
                continue
        index, err, touched = _read_targets(
            session, line, index, out, names, finals, numbers, allow
        )
        used.update(touched)
        if err:
            return None, err, used
    return "".join(out), None, used


def _skip_name(line, index):
    while index < len(line) and is_name_char(line[index]):
        index += 1
    if index < len(line) and line[index] in "$%":
        index += 1
    return index


def _skip_targets(line, index):
    while True:
        while index < len(line) and line[index] == " ":
            index += 1
        if index < len(line) and line[index].isdigit():
            while index < len(line) and line[index].isdigit():
                index += 1
        elif index < len(line) and is_letter(line[index]):
            index = _skip_name(line, index)
        else:
            return index
        nxt = index
        while nxt < len(line) and line[nxt] == " ":
            nxt += 1
        if nxt < len(line) and line[nxt] == ",":
            index = nxt + 1
            continue
        return index


def _warnings(lines):
    notes = []
    groups = {}

    def collect(line, index):
        end = index
        while end < len(line) and is_name_char(line[end]):
            end += 1
        suffix = ""
        if end < len(line) and line[end] in "$%":
            suffix = line[end]
            end += 1
        spelling = line[index:end]
        stem = spelling[:-1] if suffix else spelling
        key = stem if len(stem) == 1 else stem[:2]
        groups.setdefault((key, suffix), [])
        if spelling not in groups[(key, suffix)]:
            groups[(key, suffix)].append(spelling)
        return end

    for line_no, line in enumerate(lines, 1):
        regions = mask(line)
        index = 0
        if take_label(line):
            index = _skip_name(line.lstrip(), 0)
            index = len(line) - len(line.lstrip()) + index
            if index < len(line) and line[index] == ":":
                index += 1
        while index < len(line):
            if regions[index] in ("quote", "rem"):
                index += 1
                continue
            matched = None
            for keyword in TOKEN_KEYWORDS:
                if not any(ch.isalpha() for ch in keyword):
                    continue
                size = len(keyword)
                if index + size > len(line):
                    continue
                chunk = line[index:index + size]
                if chunk.upper() != keyword.upper():
                    continue
                if index > 0 and is_letter(line[index - 1]):
                    continue
                if index + size < len(line) and is_letter(line[index + size]):
                    continue
                matched = keyword
                if chunk != keyword:
                    notes.append(f"line {line_no}: {chunk}")
                index += size
                break
            if matched in ("GOTO", "GOSUB"):
                index = _skip_targets(line, index)
                continue
            if matched in ("THEN", "ELSE"):
                nxt = index
                while nxt < len(line) and line[nxt] == " ":
                    nxt += 1
                if word_at(line, nxt, "GOTO") or word_at(line, nxt, "GOSUB"):
                    continue
                if nxt < len(line) and is_letter(line[nxt]) and not _letter_keyword(line, nxt):
                    index = _skip_name(line, nxt)
                continue
            if matched == "FN":
                nxt = index
                while nxt < len(line) and line[nxt] == " ":
                    nxt += 1
                if nxt < len(line) and is_letter(line[nxt]):
                    index = _skip_name(line, nxt)
                continue
            if matched:
                continue
            if is_letter(line[index]):
                index = collect(line, index)
                continue
            index += 1
    for spellings in groups.values():
        if len(spellings) > 1:
            notes.append(" / ".join(spellings))
    return notes


def resolve(session, accepted, accept_map):
    if not session.ready:
        prepare(session)
    outcome = Outcome()
    blocked = False
    for item in session.items:
        if item.replacement is None:
            outcome.unresolved.append(f"{item.line_no}: {item.phrase}")
            blocked = True
            continue
        if not accepted.get(item.line_no, False):
            outcome.unresolved.append(f"{item.line_no}: {item.phrase}")
            blocked = True
    if session.renames and not accept_map:
        blocked = True
    if blocked:
        return outcome
    built = []
    proposals = {item.line_no: item for item in session.items if item.replacement}
    for line_no, line in enumerate(session.source, 1):
        if line_no in proposals:
            built.extend(proposals[line_no].replacement)
            continue
        text, _err, _used = _rewrite_line(session, line)
        built.append(line if text is None else text)
    outcome.lines = built
    outcome.warnings = _warnings(built)
    return outcome


def write_seq(lines, path):
    data = b"".join(line.encode("ascii") + b"\r" for line in lines)
    Path(path).write_bytes(data)


def prepare(session):
    if session.pending:
        return
    session.uses = {}
    finished = []
    for item in session.items:
        if item.replacement is None:
            finished.append(item)
    for line_no, line in enumerate(session.source, 1):
        if any(item.line_no == line_no and item.replacement is None for item in finished):
            continue
        text, err, used = _rewrite_line(session, line)
        for old in used:
            session.uses.setdefault(old, []).append(line_no)
        if err:
            if err in ("unknown label", "unknown line") and len(line) > 79:
                working = line
            else:
                finished.append(Block(line_no, err, EXPLAIN[err], line))
                continue
        else:
            working = text
        early = _propose_early(working)
        if early == ("same",):
            continue
        if early is not None:
            lines, reason = early
            if lines != [line]:
                finished.append(Block(line_no, reason, reason, line, lines))
            continue
        joined, notes = _join_go(working)
        work = _collapse(joined)
        if work != joined and "spaces collapsed" not in notes:
            notes.append("spaces collapsed")
        shortened, changed = _shorten_print(work)
        if changed:
            notes.append("PRINT shortened")
            work = shortened
        parts = _split_colons(work)
        if len(parts) > 1:
            notes.append("split at colons")
        done = _finish_pieces(parts)
        if isinstance(done, str):
            finished.append(Block(line_no, done, EXPLAIN[done], line))
            continue
        lines, more = done
        notes.extend(more)
        if lines != [line]:
            finished.append(Block(line_no, ", ".join(notes), ", ".join(notes), line, lines))
    session.items = finished
    session.ready = True
