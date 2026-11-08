#!/usr/bin/env python3
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bas2seq.engine import load, load_path, split_lines, supply
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
    print(checks, "checks")


if __name__ == "__main__":
    main()
