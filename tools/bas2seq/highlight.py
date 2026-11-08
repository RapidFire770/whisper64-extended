from bas2seq.scan import mask

EDITOR = [
    ("END", "statement"),
    ("FOR", "statement"),
    ("NEXT", "statement"),
    ("DATA", "statement"),
    ("INPUT#", "statement"),
    ("INPUT", "statement"),
    ("DIM", "statement"),
    ("READ", "statement"),
    ("LET", "statement"),
    ("GOTO", "statement"),
    ("RUN", "statement"),
    ("IF", "statement"),
    ("RESTORE", "statement"),
    ("GOSUB", "statement"),
    ("RETURN", "statement"),
    ("REM", "statement"),
    ("STOP", "statement"),
    ("ON", "statement"),
    ("WAIT", "statement"),
    ("LOAD", "statement"),
    ("SAVE", "statement"),
    ("VERIFY", "statement"),
    ("DEF", "statement"),
    ("POKE", "statement"),
    ("PRINT#", "statement"),
    ("PRINT", "statement"),
    ("CONT", "statement"),
    ("LIST", "statement"),
    ("CLR", "statement"),
    ("CMD", "statement"),
    ("SYS", "statement"),
    ("OPEN", "statement"),
    ("CLOSE", "statement"),
    ("GET", "statement"),
    ("NEW", "statement"),
    ("TO", "statement"),
    ("THEN", "statement"),
    ("NOT", "statement"),
    ("STEP", "statement"),
    ("AND", "statement"),
    ("OR", "statement"),
    ("GO", "statement"),
    ("TAB(", "function"),
    ("FN", "function"),
    ("SPC(", "function"),
    ("SGN", "function"),
    ("INT", "function"),
    ("ABS", "function"),
    ("USR", "function"),
    ("FRE", "function"),
    ("POS", "function"),
    ("SQR", "function"),
    ("RND", "function"),
    ("LOG", "function"),
    ("EXP", "function"),
    ("COS", "function"),
    ("SIN", "function"),
    ("TAN", "function"),
    ("ATN", "function"),
    ("PEEK", "function"),
    ("LEN", "function"),
    ("STR$", "function"),
    ("VAL", "function"),
    ("ASC", "function"),
    ("CHR$", "function"),
    ("LEFT$", "function"),
    ("RIGHT$", "function"),
    ("MID$", "function"),
]

_KEYWORD = {name: role for name, role in EDITOR}


def _word_char(ch):
    return ch.isupper() or ch in "$%#"


def _flush_word(word, start, roles):
    if len(word) <= 0 or len(word) > 15:
        return False
    key = "".join(word)
    role = _KEYWORD.get(key)
    if not role:
        return False
    for j in range(len(word)):
        roles[start + j] = role
    return key == "REM"


def spans(line):
    if not line:
        return []

    roles = ["plain"] * len(line)
    word = []
    start = 0
    quote = False
    rem = False
    too_long = False

    for i, ch in enumerate(line):
        if rem:
            break
        if ch == '"':
            if not too_long and word:
                rem = _flush_word(word, start, roles) or rem
            word = []
            too_long = False
            quote = not quote
            continue
        if quote:
            continue
        if ch == "(":
            text = "".join(word)
            if text == "TAB" or text == "SPC":
                word.append("(")
                rem = _flush_word(word, start, roles) or rem
            elif not too_long and word:
                rem = _flush_word(word, start, roles) or rem
            word = []
            too_long = False
            continue
        if _word_char(ch):
            if not word and not too_long:
                start = i
            if len(word) < 15:
                word.append(ch)
            else:
                too_long = True
        elif word or too_long:
            if not too_long and word:
                rem = _flush_word(word, start, roles) or rem
            word = []
            too_long = False

    if not rem and not too_long and word:
        _flush_word(word, start, roles)

    regions = mask(line)
    for i, ch in enumerate(line):
        if ch == "?" and regions[i] == "code":
            roles[i] = "statement"

    out = []
    i = 0
    while i < len(line):
        role = roles[i]
        j = i + 1
        while j < len(line) and roles[j] == role:
            j += 1
        out.append((line[i:j], role))
        i = j
    return out
