#!/usr/bin/env python3
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bas2seq.engine import load, load_path, prepare, resolve, split_lines, supply, write_seq
from bas2seq.scan import TOKEN_KEYWORDS


def main():
    checks = 0

    def check(name, fn):
        nonlocal checks
        checks += 1
        fn()
        print("PASS", name)

    def keywords_match_token_c():
        text = Path("src/conv/token.c").read_text()
        block = text.split("keywords[]")[1].split("NULL")[0]
        names = re.findall(r'"([^"]*)"', block)
        assert tuple(names) == TOKEN_KEYWORDS

    def split_keeps_blanks_and_final():
        assert split_lines("") == []
        assert split_lines("a") == ["a"]
        assert split_lines("a\n") == ["a"]
        assert split_lines("a\n\nb") == ["a", "", "b"]
        assert split_lines("a\r\nb\rc") == ["a", "b", "c"]

    def short_line_is_kept():
        doc = load('10 PRINT "HI"\n')
        assert doc.error is None
        assert doc.source == ['10 PRINT "HI"']
        assert doc.items == []

    def tab_is_character():
        doc = load("10 PRINT \"HI\"\t\n")
        assert len(doc.items) == 1
        assert doc.items[0].line_no == 1
        assert doc.items[0].phrase == "character"
        assert doc.items[0].replacement is None

    def brace_is_kept():
        doc = load('10 PRINT "{"\n')
        assert doc.items == []

    def line_number_rules():
        assert load("10PRINT \"HI\"\n").items == []
        assert load("63999 PRINT \"HI\"\n").items == []
        bad = load("64000 PRINT \"HI\"\n")
        assert bad.items[0].phrase == "line number"
        assert bad.items[0].line_no == 1
        dup = load("10 PRINT \"A\"\n20 PRINT \"B\"\n10 PRINT \"C\"\n")
        assert [item.phrase for item in dup.items] == ["duplicate line"]
        assert dup.items[0].line_no == 3

    def missing_file():
        doc = load_path("/tmp/bas2seq-no-such-file.bas")
        assert doc.error
        assert "not UTF-8" not in doc.error

    def label_prefix_rules():
        doc = load(
            "labelSetup:\n"
            "labelActivateSel:\n"
            "labelStartSelRoll:\n"
            "labelChooseDrive:\n"
            "labelChooseDriveKP:\n"
        )
        assert doc.auto["labelActivateSel"] == "ActivateSel"
        assert doc.auto["labelChooseDrive"] == "ChooseDrive"
        assert "labelSetup" not in doc.auto
        assert doc.pending == ["labelStartSelRoll", "labelChooseDriveKP"]
        assert supply(doc, "labelStartSelRoll", "") == "empty"
        assert supply(doc, "labelStartSelRoll", "StartSelRollExtra") == "too long"
        assert supply(doc, "labelStartSelRoll", "ChooseDrive") == "already used"
        assert supply(doc, "labelStartSelRoll", "StartRoll") is None
        assert doc.pending == ["labelChooseDriveKP"]

    def duplicate_label_blocks():
        doc = load("labelSetup:\nlabelSetup:\n")
        assert [item.phrase for item in doc.items] == ["duplicate label"]
        assert doc.items[0].line_no == 2
        assert doc.pending == []

    def underscore_label_blocks():
        doc = load("label_foo:\n")
        assert doc.items[0].phrase == "label character"
        assert doc.items[0].line_no == 1

    def statement_not_label_character():
        for line in (
            "B1$=CHR$(180):B2$=CHR$(170)",
            "R=0: REM NOTE",
            "GET#1,A$,B$: REM EAT",
        ):
            doc = load(line + "\n")
            assert not any(item.phrase == "label character" for item in doc.items), line
        bad = load("labelLDN$xt:\n")
        assert bad.items[0].phrase == "label character"

    def then_else_assignments():
        src = (
            "10 IF A THEN B = 1\n"
            "20 IF A THEN M$ = \"*\"\n"
            "30 IF A THEN SI(ND) = 6\n"
            "40 IF A THEN DN$(ND)=\"X\"\n"
        )
        doc = load(src)
        prepare(doc)
        assert not any(item.phrase == "unknown label" for item in doc.items)

    def label_comment_keeps_colon():
        body = "labelFoo: REM " + "word " * 30
        assert len(body) > 79
        doc = load(body + "\n")
        prepare(doc)
        assert doc.items[0].replacement[0].startswith("labelFoo:")
        assert all(len(part) <= 79 for part in doc.items[0].replacement)

    def resolve_blocked_while_pending():
        doc = load("labelStartSelRoll:\nGOTO labelStartSelRoll\n")
        assert doc.pending == ["labelStartSelRoll"]
        out = resolve(doc, {}, True)
        assert out.lines is None

    def ladder_stops_early():
        spaced = "PRINT" + " " * 71 + '"HI"'
        assert len(spaced) > 79
        doc = load(spaced + "\n")
        prepare(doc)
        assert doc.items[0].replacement == ['PRINT' + " " + '"HI"']
        assert doc.items[0].detail == "spaces collapsed"

        short = 'PRINT "HI"\n'
        assert load(short).items == []
        kept = load('10 PRINT "HI": REM NOTE\n')
        prepare(kept)
        assert kept.items == []

        padded = 'PRINT "HI":' + " " * 70 + "REM " + "X" * 70
        doc = load(padded + "\n")
        prepare(doc)
        assert doc.items[0].replacement == ['PRINT "HI"']
        assert "spaces collapsed" in doc.items[0].detail
        assert "comment removed" in doc.items[0].detail
        assert "PRINT shortened" not in doc.items[0].detail

        inside = 'PRINT "' + "  " + ("Q" * 70) + '"'
        assert len(inside) > 79
        doc = load(inside + "\n")
        prepare(doc)
        assert "  " in doc.items[0].replacement[0]

        long_print = "PRINT " + '"AB"' * 19
        assert len(long_print) > 79
        doc = load(long_print + "\n")
        prepare(doc)
        assert doc.items[0].replacement[0].startswith("?")
        assert "PRINT#" not in doc.items[0].replacement[0]
        sharp = "PRINT#1, " + " " * 72 + '"AB"'
        doc = load(sharp + "\n")
        prepare(doc)
        assert "PRINT#" in doc.items[0].replacement[0]
        assert "PRINT shortened" not in doc.items[0].detail

    def splits_and_wraps():
        line = 'labelFoo: X$ = "' + "A" * 60 + '": GOTO labelBar'
        assert len(line) > 79
        doc = load(line + "\nlabelBar:\n")
        prepare(doc)
        assert doc.items[0].replacement == [
            'labelFoo: X$ = "' + "A" * 60 + '"',
            "GOTO labelBar",
        ]
        assert "split at colons" in doc.items[0].detail

        quoted = 'X$ = "A:B":' + "Q" * 70
        assert len(quoted) > 79
        doc = load(quoted + "\n")
        prepare(doc)
        assert any('"A:B"' in part for part in doc.items[0].replacement)

        empty = 'X$ = "A"::X$ = "B":' + "Y" * 70
        doc = load(empty + "\n")
        prepare(doc)
        assert all(part.strip() != "" for part in doc.items[0].replacement)

        rem = "REM " + "word " * 20
        assert len(rem) > 79
        doc = load(rem.rstrip() + "\n")
        prepare(doc)
        assert doc.items[0].phrase != "comment word"
        assert all(part.startswith("REM ") and len(part) <= 79 for part in doc.items[0].replacement)
        assert "comment wrapped" in doc.items[0].detail

        huge = "REM " + ("N" * 76)
        doc = load(huge + "\n")
        prepare(doc)
        assert doc.items[0].phrase == "comment word"
        assert doc.items[0].replacement is None

        assign = 'TP$ = "' + "A" * 40 + '" + "' + "B" * 40 + '" + "' + "C" * 40 + '"'
        assert len(assign) > 79
        doc = load(assign + "\n")
        prepare(doc)
        assert doc.items[0].replacement[0].startswith("TP$ = ")
        assert doc.items[0].replacement[1].startswith("TP$ = TP$ + ")
        assert all(len(part) <= 79 for part in doc.items[0].replacement)
        assert "assignment split" in doc.items[0].detail

        one = 'PRINT "' + "Z" * 80 + '"'
        doc = load(one + "\n")
        prepare(doc)
        assert doc.items[0].phrase == "line length"
        assert doc.items[0].replacement is None

    def jumps_warnings_and_write(tmp_path=None):
        import tempfile
        from pathlib import Path
        doc = load("labelFoo:\nGO TO labelFoo\nGOTO missing\n10 GOTO 20\n")
        prepare(doc)
        go = [item for item in doc.items if item.line_no == 2][0]
        assert go.replacement == ["GOTO labelFoo"]
        assert go.detail == "GO TO joined"
        missing = [item for item in doc.items if item.line_no == 3][0]
        assert missing.phrase == "unknown label"
        assert missing.replacement is None
        unknown = [item for item in doc.items if item.line_no == 4][0]
        assert unknown.phrase == "unknown line"

        kept = load('10 IF A THEN PRINT "B"\n20 GOTO 10\n')
        prepare(kept)
        assert kept.items == []

        doc = load("labelActivateSel:\nGOTO labelActivateSel\n")
        prepare(doc)
        assert doc.items == []
        assert doc.uses["labelActivateSel"] == [1, 2]
        assert doc.renames == [("labelActivateSel", "ActivateSel")]

        out = resolve(doc, {}, True)
        assert out.lines == ["ActivateSel:", "GOTO ActivateSel"]
        assert out.unresolved == []

        refused = resolve(doc, {}, False)
        assert refused.lines is None

        warn = load('10 print "A"\n20 COLOR = 1: COLCODE = 2\n')
        prepare(warn)
        out = resolve(warn, {}, True)
        assert any("print" in note for note in out.warnings)
        assert any("COLCODE" in note and "COLOR" in note for note in out.warnings)
        quiet = load("10 CO$ = \"A\": COLCODE = 1\n20 GOTO labelFoo\nlabelFoo:\n")
        prepare(quiet)
        out = resolve(quiet, {}, True)
        assert not any("COLCODE" in note for note in out.warnings)

        blocked = load("GOTO nowhere\n")
        prepare(blocked)
        out = resolve(blocked, {}, True)
        assert out.lines is None

        gone = load("GO TO nowhere\nGO SUB nowhere\n")
        prepare(gone)
        assert gone.items[0].phrase == "unknown label"
        assert gone.items[0].replacement is None
        assert gone.items[1].phrase == "unknown label"
        assert resolve(gone, {}, True).lines is None

        renamed = load("labelActivateSel:\nGO TO labelActivateSel\n")
        prepare(renamed)
        go = [item for item in renamed.items if item.line_no == 2][0]
        assert go.replacement == ["GOTO ActivateSel"]
        assert go.detail == "GO TO joined"
        assert renamed.uses["labelActivateSel"] == [1, 2]
        out = resolve(renamed, {2: True}, True)
        assert out.lines == ["ActivateSel:", "GOTO ActivateSel"]
        folder = Path(tempfile.mkdtemp())
        target = folder / "out.seq"
        write_seq(["10 PRINT \"HI\"", ""], str(target))
        raw = target.read_bytes()
        assert raw == b'10 PRINT "HI"\r\r'
        assert b"\n" not in raw

    check("keywords", keywords_match_token_c)
    check("split", split_keeps_blanks_and_final)
    check("short", short_line_is_kept)
    check("tab", tab_is_character)
    check("brace", brace_is_kept)
    check("numbers", line_number_rules)
    check("missing", missing_file)
    check("prefix", label_prefix_rules)
    check("dup-label", duplicate_label_blocks)
    check("underscore", underscore_label_blocks)
    check("stmt-not-label", statement_not_label_character)
    check("then-else-assign", then_else_assignments)
    check("label-rem-colon", label_comment_keeps_colon)
    check("pending-resolve", resolve_blocked_while_pending)
    check("ladder-early", ladder_stops_early)
    check("splits", splits_and_wraps)
    def sample_fixture():
        text = Path("tools/fixtures/bas2seq_sample.bas").read_text()
        tp = [line for line in text.splitlines() if line.startswith("TP$")][0]
        assert len(tp) == 215
        doc = load(text)
        assert doc.pending == ["labelStartSelRoll", "labelChooseDriveKP"]
        assert supply(doc, "labelStartSelRoll", "StartRoll") is None
        assert supply(doc, "labelChooseDriveKP", "PickDrv") is None
        prepare(doc)
        accepted = {item.line_no: True for item in doc.items if item.replacement is not None}
        out = resolve(doc, accepted, True)
        assert out.lines is not None
        assert all(len(line) <= 79 for line in out.lines)
        assert any(line.startswith("GOTO ") for line in out.lines)
        joined = "\n".join(out.lines)
        assert "StartRoll" in joined
        assert "PickDrv" in joined
        assert "labelStartSelRoll" not in joined
        assert "ChooseDrive" in joined
        assert "labelChooseDrive" not in joined

    check("jumps", jumps_warnings_and_write)
    check("sample", sample_fixture)

    def colors_match_editor_and_scan():
        import re
        from bas2seq.highlight import EDITOR, spans
        text = Path("src/editor_state.c").read_text()
        names = re.findall(r'"([^"]*)"', text.split("basic_keywords[]")[1].split("NULL")[0])
        colors = re.findall(r"COL_(PURPLE|BLUE)", text.split("basic_colors[]")[1].split("};")[0])
        role = {"PURPLE": "statement", "BLUE": "function"}
        assert EDITOR == [(name, role[color]) for name, color in zip(names, colors)]

        def roles(line):
            return spans(line)

        def paint(line, role_name):
            return "".join(text for text, role in roles(line) if role == role_name)

        assert "PRINT" in paint('PRINT "PRINT" : GOTO 10', "statement")
        assert "GOTO" in paint('PRINT "PRINT" : GOTO 10', "statement")
        assert paint('PRINT "PRINT"', "statement") == "PRINT"
        assert "SGN" in paint("X = SGN(1)", "function")
        assert "CHR$" in paint('A$ = CHR$(65)', "function")
        assert paint('?"HI"', "statement") == "?"
        rem = paint("REM GOTO PRINT", "statement")
        assert rem == "REM"
        assert paint("TAB(10)", "function") == "TAB("
        assert paint("print", "statement") == ""
        assert paint("REMEMBER", "statement") == ""

    check("colors", colors_match_editor_and_scan)

    def browser_lists_parent_dirs_then_files():
        from bas2seq.browser import list_entries

        root = Path(tempfile.mkdtemp())
        inside = root / "inside"
        inside.mkdir()
        (root / "outside.bas").write_text('10 PRINT "HI"\n', encoding="utf-8")
        (inside / "stay.bas").write_text("20 END\n", encoding="utf-8")
        (inside / "sub").mkdir()
        (inside / "Bdir").mkdir()
        here = list_entries(inside)
        assert [row.label for row in here] == ["..", "Bdir/", "sub/", "stay.bas"]
        assert here[0].kind == "up"
        assert here[0].path == inside.resolve().parent
        assert [row.kind for row in here] == ["up", "dir", "dir", "file"]
        parent = list_entries(here[0].path)
        assert [row.label for row in parent] == ["..", "inside/", "outside.bas"]
        assert any(row.path == (root / "outside.bas").resolve() for row in parent)
        top = list_entries(Path("/"))
        assert top
        assert all(row.kind != "up" for row in top)

    check("browser-list", browser_lists_parent_dirs_then_files)

    def browser_unreadable_keeps_parent():
        from bas2seq.browser import list_entries

        if hasattr(os, "geteuid") and os.geteuid() == 0:
            return
        root = Path(tempfile.mkdtemp())
        locked = root / "locked"
        locked.mkdir()
        locked.chmod(0)
        try:
            rows = list_entries(locked)
        finally:
            locked.chmod(0o755)
        assert [row.kind for row in rows] == ["up"]
        assert rows[0].path == root.resolve()

    check("browser-locked", browser_unreadable_keeps_parent)

    print(checks, "checks")


if __name__ == "__main__":
    main()
