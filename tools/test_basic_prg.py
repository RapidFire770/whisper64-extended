#!/usr/bin/env python3
"""Byte checks for a BASIC V2 PRG.

The arrays marked hand-checked were counted from $0801, not produced by the
oracle. src/conv/token.c follows these same rules. The ROM does not store
the spaces between the line number and the first statement byte.
"""

KEYWORDS = [
    "END", "FOR", "NEXT", "DATA", "INPUT#", "INPUT", "DIM", "READ", "LET", "GOTO",
    "RUN", "IF", "RESTORE", "GOSUB", "RETURN", "REM", "STOP", "ON", "WAIT", "LOAD",
    "SAVE", "VERIFY", "DEF", "POKE", "PRINT#", "PRINT", "CONT", "LIST", "CLR", "CMD",
    "SYS", "OPEN", "CLOSE", "GET", "NEW", "TAB(", "TO", "FN", "SPC(", "THEN",
    "NOT", "STEP", "+", "-", "*", "/", "^", "AND", "OR", ">",
    "=", "<", "SGN", "INT", "ABS", "USR", "FRE", "POS", "SQR", "RND",
    "LOG", "EXP", "COS", "SIN", "TAN", "ATN", "PEEK", "LEN", "STR$", "VAL",
    "ASC", "CHR$", "LEFT$", "RIGHT$", "MID$", "GO",
]

TOKEN = {name: 0x80 + i for i, name in enumerate(KEYWORDS)}
MAX_LINE = 63999


def tokenize_stmt(line):
    """Return (code, line_no, statement).

    code >= 0 is the statement length. -1 blank, -2 no number, -3 too big.
    """
    i = 0
    while i < len(line) and line[i] == " ":
        i += 1
    if i >= len(line):
        return -1, None, b""
    if not line[i].isdigit():
        return -2, None, b""

    num = 0
    while i < len(line) and line[i].isdigit():
        digit = ord(line[i]) - ord("0")
        if num > MAX_LINE // 10 or (num == MAX_LINE // 10 and digit > MAX_LINE % 10):
            return -3, None, b""
        num = num * 10 + digit
        i += 1
    while i < len(line) and line[i] == " ":
        i += 1

    src = line[i:]
    out = bytearray()
    quote = False
    rem = False
    data = False
    p = 0
    while p < len(src):
        ch = src[p]
        if rem:
            out.append(ord(ch))
            p += 1
            continue
        if quote:
            out.append(ord(ch))
            if ch == '"':
                quote = False
            p += 1
            continue
        if ch == '"':
            out.append(ord(ch))
            quote = True
            p += 1
            continue
        if data:
            if ch == ":":
                data = False
            out.append(ord(ch))
            p += 1
            continue
        if ch == "?":
            out.append(TOKEN["PRINT"])
            p += 1
            continue
        matched = False
        for name in KEYWORDS:
            if src.startswith(name, p):
                tok = TOKEN[name]
                out.append(tok)
                p += len(name)
                if tok == TOKEN["REM"]:
                    rem = True
                elif tok == TOKEN["DATA"]:
                    data = True
                matched = True
                break
        if not matched:
            out.append(ord(ch))
            p += 1
    return len(out), num, bytes(out)


def editor_line(line_no, stmt):
    """Match conv_format_line. (None, True) when the first statement does not fit."""
    text = str(line_no)
    mark = None
    state = 0
    started = False
    for b in stmt:
        old = state
        sep = b == ord(":") and old in (0, 3)
        if old in (1, 2, 3, 4):
            piece = bytes([b]).decode("latin1")
            if old == 4 and b == 0x22:
                state = 3
            elif old == 3 and b == 0x22:
                state = 4
            elif old == 3 and b == ord(":"):
                state = 0
            elif old == 1 and b == 0x22:
                state = 0
        elif b == 0x22:
            state = 1
            piece = '"'
        elif 0x80 <= b <= 0xCB:
            piece = KEYWORDS[b - 0x80]
            if b == TOKEN["REM"]:
                state = 2
            elif b == TOKEN["DATA"]:
                state = 3
        else:
            piece = bytes([b]).decode("latin1")
        gap = " " if not started and not piece.startswith(" ") else ""
        if len(text) + len(gap) + len(piece) >= 80:
            if sep:
                return text, True
            if mark is None:
                return None, True
            return text[:mark], True
        text += gap + piece
        started = True
        if sep:
            mark = len(text) - 1
    return text, False


def build_prg(lines):
    addr = 0x0801
    body = bytearray()
    for line in lines:
        code, num, stmt = tokenize_stmt(line)
        if code == -1:
            continue
        if code < 0:
            raise AssertionError((line, code))
        nxt = addr + 4 + len(stmt) + 1
        body.append(nxt & 0xFF)
        body.append(nxt >> 8)
        body.append(num & 0xFF)
        body.append(num >> 8)
        body.extend(stmt)
        body.append(0)
        addr = nxt
    body.extend(b"\x00\x00")
    return bytes([0x01, 0x08]) + bytes(body)


def detokenize_prg(prg):
    if prg[:2] != b"\x01\x08":
        raise AssertionError("load address")
    addr = 0x0801
    pos = 2
    lines = []
    while True:
        nxt = prg[pos] | (prg[pos + 1] << 8)
        if nxt == 0:
            break
        num = prg[pos + 2] | (prg[pos + 3] << 8)
        end = pos + (nxt - addr)
        stmt = prg[pos + 4:end - 1]
        if prg[end - 1] != 0:
            raise AssertionError("missing line end")
        text, _trunc = editor_line(num, stmt)
        lines.append(text)
        pos = end
        addr = nxt
    return lines


# Counted from $0801 the way the ROM crunches: no space between the line
# number and PRINT. LIST inserts that space on the way out.
HAND_EMPTY = bytes([0x01, 0x08, 0x00, 0x00])
HAND_PRINT = bytes([
    0x01, 0x08,
    0x07, 0x08,
    0x0A, 0x00,
    0x99, 0x00,
    0x00, 0x00,
])
HAND_PLUS = bytes([
    0x01, 0x08,
    0x0B, 0x08,
    0x0A, 0x00,
    0x99, 0x20, 0x31, 0xAA, 0x31, 0x00,
    0x00, 0x00,
])
# Published line image of 10 PRINT 2*3, plus the $0801 load address.
HAND_STAR = bytes([
    0x01, 0x08,
    0x0B, 0x08,
    0x0A, 0x00,
    0x99, 0x20, 0x32, 0xAC, 0x33, 0x00,
    0x00, 0x00,
])


def seq_text(lines):
    """Editor SEQ: CR after every line, including the last."""
    body = "\r".join(lines)
    if not lines:
        return b"\r"
    return (body + "\r").encode("latin1")


def main():
    assert len(KEYWORDS) == 76
    assert TOKEN["END"] == 0x80
    assert TOKEN["PRINT"] == 0x99
    assert TOKEN["+"] == 0xAA
    assert TOKEN["REM"] == 0x8F
    assert TOKEN["GO"] == 0xCB
    assert TOKEN["GOTO"] == 0x89

    assert build_prg([]) == HAND_EMPTY
    assert build_prg(["10 PRINT"]) == HAND_PRINT
    assert build_prg(["10 PRINT 1+1"]) == HAND_PLUS
    assert build_prg(["10 PRINT 2*3"]) == HAND_STAR
    assert build_prg(["10  PRINT"]) == HAND_PRINT
    assert HAND_PLUS[9] == 0xAA

    code, num, stmt = tokenize_stmt('10 PRINT "LOAD"')
    assert code > 0 and num == 10
    assert TOKEN["LOAD"] not in stmt
    assert stmt.endswith(b'"LOAD"')

    code, num, stmt = tokenize_stmt('10 PRINT "\x93"')
    assert b'"\x93"' in stmt
    assert detokenize_prg(build_prg(['10 PRINT "\x93"'])) == ['10 PRINT "\x93"']

    code, _num, stmt = tokenize_stmt("10 REM PRINT")
    assert TOKEN["PRINT"] not in stmt
    assert b"PRINT" in stmt

    code, _num, stmt = tokenize_stmt("10 REM \x93")
    assert 0x93 in stmt

    _code, _num, stmt = tokenize_stmt("10 DATA PRINT")
    assert TOKEN["DATA"] in stmt and TOKEN["PRINT"] not in stmt
    assert b"PRINT" in stmt

    _code, _num, stmt = tokenize_stmt("10 DATA PRINT:PRINT")
    assert stmt.count(bytes([TOKEN["PRINT"]])) == 1
    assert b"PRINT:" in stmt
    assert detokenize_prg(build_prg(["10 DATA PRINT:PRINT"])) == ["10 DATA PRINT:PRINT"]

    _code, _num, stmt = tokenize_stmt('10 DATA "PRINT"')
    assert TOKEN["PRINT"] not in stmt
    assert b"PRINT" in stmt

    _code, _num, goto = tokenize_stmt("10 GOTO 20")
    _code, _num, go = tokenize_stmt("10 GO TO 20")
    assert TOKEN["GOTO"] in goto
    assert TOKEN["GO"] in go and TOKEN["TO"] in go
    assert TOKEN["GOTO"] not in go

    _code, _num, stmt = tokenize_stmt('10 ? "A"')
    assert TOKEN["PRINT"] in stmt
    assert b"?" not in stmt
    assert editor_line(10, stmt)[0] == '10 PRINT "A"'

    _code, _num, stmt = tokenize_stmt("10 PRINT \xff")
    assert 0xFF in stmt
    assert editor_line(10, stmt)[0] == "10 PRINT \xff"

    assert tokenize_stmt("64000 PRINT")[0] == -3
    assert tokenize_stmt("   ")[0] == -1
    assert tokenize_stmt("")[0] == -1
    assert tokenize_stmt("PRINT")[0] == -2

    code, num, stmt = tokenize_stmt("10PRINT")
    assert num == 10 and stmt == bytes([TOKEN["PRINT"]])
    assert editor_line(10, stmt)[0] == "10 PRINT"

    code, num, stmt = tokenize_stmt("10")
    assert code == 0 and num == 10 and stmt == b""

    assert detokenize_prg(HAND_PRINT) == ["10 PRINT"]
    assert detokenize_prg(HAND_PLUS) == ["10 PRINT 1+1"]
    assert detokenize_prg(build_prg(["10 PRINT", "20 GOTO 10"])) == [
        "10 PRINT",
        "20 GOTO 10",
    ]

    # Twenty PRINT tokens have no colon, so the line is refused.
    packed = bytes([TOKEN["PRINT"]]) * 20
    text, truncated = editor_line(10, packed)
    assert text is None and truncated

    code, num, stmt = tokenize_stmt(
        '3 B1$=CHR$(180):B2$=CHR$(170):BL$=CHR$(108):BR$=CHR$(186):'
        'BB$=CHR$(175):TB$=CHR$(183)'
    )
    assert num == 3
    text, truncated = editor_line(num, stmt)
    assert truncated
    assert text == (
        '3 B1$=CHR$(180):B2$=CHR$(170):BL$=CHR$(108):BR$=CHR$(186):BB$=CHR$(175)'
    )

    exact = '10 PRINT "' + ('X' * 68) + '"'
    code, num, stmt = tokenize_stmt(exact + ':REM HI')
    assert code > 0 and len(exact) == 79
    text, truncated = editor_line(num, stmt)
    assert truncated and text == exact

    code, _num, stmt = tokenize_stmt("10 INPUT#1,A")
    assert TOKEN["INPUT#"] in stmt and TOKEN["INPUT"] not in stmt

    code, _num, stmt = tokenize_stmt('10 PRINT#1,"A"')
    assert TOKEN["PRINT#"] in stmt and TOKEN["PRINT"] not in stmt

    code, _num, stmt = tokenize_stmt('10 DATA "A:B":PRINT')
    assert b'"A:B"' in stmt
    assert stmt.count(bytes([TOKEN["PRINT"]])) == 1

    assert seq_text(["10 PRINT", "20 GOTO 10"]) == b"10 PRINT\r20 GOTO 10\r"
    assert seq_text(["10 PRINT"]) == b"10 PRINT\r"

    print("ok", len(HAND_PLUS), "plus-file bytes")


if __name__ == "__main__":
    main()
