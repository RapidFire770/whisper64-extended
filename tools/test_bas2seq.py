#!/usr/bin/env python3
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bas2seq.engine import load, load_path, prepare, split_lines, supply
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
        doc = load(line + "\n")
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
    check("ladder-early", ladder_stops_early)
    check("splits", splits_and_wraps)
    print(checks, "checks")


if __name__ == "__main__":
    main()
