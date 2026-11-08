TOKEN_KEYWORDS = (
    "END", "FOR", "NEXT", "DATA", "INPUT#", "INPUT", "DIM", "READ", "LET", "GOTO",
    "RUN", "IF", "RESTORE", "GOSUB", "RETURN", "REM", "STOP", "ON", "WAIT", "LOAD",
    "SAVE", "VERIFY", "DEF", "POKE", "PRINT#", "PRINT", "CONT", "LIST", "CLR", "CMD",
    "SYS", "OPEN", "CLOSE", "GET", "NEW", "TAB(", "TO", "FN", "SPC(", "THEN",
    "NOT", "STEP", "+", "-", "*", "/", "^", "AND", "OR", ">",
    "=", "<", "SGN", "INT", "ABS", "USR", "FRE", "POS", "SQR", "RND",
    "LOG", "EXP", "COS", "SIN", "TAN", "ATN", "PEEK", "LEN", "STR$", "VAL",
    "ASC", "CHR$", "LEFT$", "RIGHT$", "MID$", "GO",
)


def is_letter(ch):
    return ("A" <= ch <= "Z") or ("a" <= ch <= "z")


def is_name_char(ch):
    return is_letter(ch) or ("0" <= ch <= "9")


def word_at(text, index, keyword):
    """Length of keyword at index, or 0. A letter on either side rejects it."""
    size = len(keyword)
    if index > 0 and is_letter(text[index - 1]):
        return 0
    if index + size > len(text) or text[index:index + size] != keyword:
        return 0
    end = index + size
    if end < len(text) and is_letter(text[end]):
        return 0
    return size


def mask(text):
    """One region name per character: code, quote, rem, or data."""
    regions = []
    quote = False
    rem = False
    data = False
    i = 0
    while i < len(text):
        if rem:
            regions.append("rem")
            i += 1
            continue
        if quote:
            regions.append("quote")
            if text[i] == '"':
                quote = False
            i += 1
            continue
        if text[i] == '"':
            regions.append("quote")
            quote = True
            i += 1
            continue
        if data:
            if text[i] == ":":
                data = False
                regions.append("code")
            else:
                regions.append("data")
            i += 1
            continue
        if word_at(text, i, "REM"):
            regions.append("code")
            rem = True
            i += 1
            continue
        if word_at(text, i, "DATA"):
            regions.append("code")
            data = True
            i += 1
            continue
        regions.append("code")
        i += 1
    return regions
