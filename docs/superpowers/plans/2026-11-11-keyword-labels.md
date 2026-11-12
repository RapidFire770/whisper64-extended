# Keyword lines are not labels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A line whose first name is exactly a BASIC V2 token stays a statement in `bas2seq` and in `W64LABEL`.

**Architecture:** Both programs already decide a label in one function, `take_label`. That function keeps its shape: optional spaces, a letter, letters or digits, then `:`. After the colon is confirmed, the whole name is compared with the V2 token table. A match returns "not a label". Callers that build the map, split on colons, drop a label-only line, or rewrite jumps all go through that function, so they pick up the rule without a second check. `PRINT1:` stays a label because the whole name has to match. `print:` stays a label because the table is uppercase.

**Tech Stack:** Python 3 for `tools/bas2seq`, C for `src/conv/renumber.c` and `src/conv/token.c`. Host checks only. No C64 link change.

## Global Constraints

- The token list is the array in `src/conv/token.c`. `tools/bas2seq/scan.py` `TOKEN_KEYWORDS` is that same tuple, in that order. Do not add a second list. Do not merge it with the editor highlight names in `src/editor_state.c`.
- The compare is the whole name, and it is case-sensitive. `PRINT:` and `SGN:` are statements. `PRINT1:` is a label. `print:` and `Print:` are labels.
- `conv_keyword_len` returns the length of the first token that matches at an index. On `PRINT1:` it returns 5, because the character after `PRINT` is a digit and the boundary check only rejects a following letter. Reject the label only when that length equals the name length.
- A token that contains `$`, `#`, or `(` never reaches the compare. The name scan stops on that character, and the next character is not `:`.
- A keyword after `THEN` or `ELSE` stays a statement. That path already calls `conv_keyword_len`. Do not change it.
- One space after a real label's colon is still stripped. A label with nothing after the colon still shares the next code line's number.
- `bas2seq` does not number lines and does not link into the editor or the three tools.
- Do not add a table, a buffer, or BSS in `renumber.c`. `W64LABEL` stays `-Oz`. Heap stays at or below `$C800`.
- Do not run `tools/test_w64conv_c64u.py`. It resets the Ultimate.
- Host checks, from the repo root: `python3 tools/test_bas2seq.py`, and `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c && /tmp/test_renumber`.

## Root cause

`take_label` in `tools/bas2seq/engine.py` and the static `take_label` in `src/conv/renumber.c` accept any leading `name:`. `PRINT` is a legal name, so the statement separator is stored as a label colon.

Loading `/home/josh/Documents/Programming/C64/dice-roll/src/main.bas` records six labels named `PRINT`: line 120 is the definition, and lines 220, 326, 336, 365, and 379 are `duplicate label`. The screen shows line 220 first. A single `PRINT:` is also wrong. `labels_to_numbered` on `PRINT: PRINT "HI"\r` returns success and the bytes `10 PRINT "HI"\r`. The leading `PRINT`, which prints a blank line, is discarded with the label. A second such line returns code 8 (`DUP LABEL`).

`tools/test_renumber.c` currently requires that behavior under the name `kw label`:

```c
expect_labels("kw label", "PRINT: END\rGOTO PRINT\rIF X THEN PRINT \"Z\"",
              "10 END\r20 GOTO 10\r30 IF X THEN PRINT \"Z\"", 10);
```

That expectation is replaced in Task 2. `IF X THEN PRINT` stays a statement. `GOTO PRINT`, with no label `PRINT`, is code 6 (`NO LABEL`).

## File map

| File | Responsibility |
|---|---|
| `tools/bas2seq/engine.py` | `take_label` rejects an exact `TOKEN_KEYWORDS` name |
| `tools/test_bas2seq.py` | Host check that `PRINT:` is not a duplicate label and a long `NEXT:` line splits at the colon |
| `docs/superpowers/specs/2026-11-08-bas-to-seq-design.md` | Label rule in the Labels and jumps section |
| `src/conv/renumber.c` | `take_label` rejects a name whose length equals `conv_keyword_len` |
| `tools/test_renumber.c` | Replace `kw label`. Keep the statement, and number both copies |
| `AGENTS.md` | One sentence on the label rule in the W64LABEL paragraph |
| `docs/Todo-List.md` | Check off the bas2seq keyword-label bug |

---

### Task 1: bas2seq leaves token names as statements

**Files:**
- Modify: `tools/bas2seq/engine.py` (`take_label`, around lines 105–118)
- Modify: `tools/test_bas2seq.py` (new check next to `duplicate_label_blocks`, registered next to `check("dup-label", ...)`)
- Modify: `docs/superpowers/specs/2026-11-08-bas-to-seq-design.md` (Labels and jumps, the paragraph that starts "A label is the start of a line")
- Test: `tools/test_bas2seq.py`

**Interfaces:**
- Consumes: `TOKEN_KEYWORDS` from `bas2seq.scan`, `load`, `prepare`
- Produces: `take_label(text)` still returns the label string, or `None`. `None` now also means the whole name is an entry in `TOKEN_KEYWORDS`. Every current caller (`load`, `_defined_names`, `_drop_comment`, `_split_colons`, `_split_assign`, `_final_labels`, `_rewrite_line`, `_warnings`) keeps calling `take_label` and does not grow its own keyword test.

- [ ] **Step 1: Write the failing test**

In `tools/test_bas2seq.py`, add this function after `duplicate_label_blocks`. Register it in `main` immediately after `check("dup-label", duplicate_label_blocks)`:

```python
    check("keyword-not-label", keyword_not_label)
```

```python
    def keyword_not_label():
        doc = load(
            'PRINT: PRINT "F1/SPACE TO ROLL AGAIN": PRINT "ANY KEY FOR MENU"\n'
            'PRINT:PRINT "PRESS ANY KEY"\n'
            'SGN: PRINT "X"\n'
            'labelMenu: PRINT: PRINT "HI"\n'
            'GOTO labelMenu\n'
        )
        assert doc.items == []
        prepare(doc)
        assert doc.items == []

        lower = load("print:\nprint:\n")
        assert [item.phrase for item in lower.items] == ["duplicate label"]
        mixed = load("Print:\nPrint:\n")
        assert [item.phrase for item in mixed.items] == ["duplicate label"]

        jump = load("GOTO PRINT\n")
        prepare(jump)
        assert jump.items[0].phrase == "unknown label"

        near = load("PRINT1:\nGOTO PRINT1\n")
        assert near.items == []
        prepare(near)
        assert near.items == []

        second = 'X$ = "' + ("A" * 70) + '"'
        line = "NEXT: " + second
        assert len(line) == 83
        assert len(second) == 77
        doc = load(line + "\n")
        prepare(doc)
        assert doc.items[0].replacement == ["NEXT", second]
        assert "split at colons" in doc.items[0].detail
```

`labelMenu: PRINT: PRINT "HI"` stays one label, `labelMenu`. The later `PRINT:` is a statement separator on that same line. `print:` and `Print:` stay labels, so the second copy is still `duplicate label`. `GOTO PRINT` has no definition. `PRINT1` is not a token. `NEXT: ` plus a 77-character assignment is 83 characters, so the ladder reaches the colon split and the token colon is a split point.

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 tools/test_bas2seq.py`

Expected: `FAIL keyword-not-label` before any later `PASS` lines. The first assertion fails because `load` appends `duplicate label` for the second `PRINT:` line. The earlier checks still print `PASS`.

- [ ] **Step 3: Reject an exact token name in `take_label`**

Replace `take_label` in `tools/bas2seq/engine.py` with:

```python
def take_label(text):
    """Return the label name when the line starts with a legal or too-long label.
    None when the line does not start with name characters and a colon.
    None when that name is exactly one TOKEN_KEYWORDS entry."""
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
    name = text[start:i]
    if name in TOKEN_KEYWORDS:
        return None
    return name
```

`TOKEN_KEYWORDS` is already imported at the top of `engine.py`. Do not case-fold `name`. Do not test a prefix.

In `docs/superpowers/specs/2026-11-08-bas-to-seq-design.md`, add this sentence at the end of the paragraph that begins "A label is the start of a line":

```markdown
A name that is exactly one entry in `TOKEN_KEYWORDS` is not a label. The compare is case-sensitive, so `PRINT:` and `SGN:` are statements, and `print:` is still a label. `PRINT1:` is a label because the whole name has to match.
```

Leave the following sentence, "The map is built before the line walk", where it is. Leave the sentence that says the editor and the three tools stay as they are. Numbering still belongs to `W64LABEL`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 tools/test_bas2seq.py`

Expected: every check prints `PASS`, including `keyword-not-label`, `dup-label`, `then-else-assign`, `label-rem-colon`, and `splits`. The last line is `<n> checks`.

- [ ] **Step 5: Commit**

```bash
git add tools/bas2seq/engine.py tools/test_bas2seq.py docs/superpowers/specs/2026-11-08-bas-to-seq-design.md
git commit -m "$(cat <<'EOF'
Keep BASIC tokens from being read as bas2seq labels.

PRINT: is a statement separator, so a second copy was reported as a duplicate label.
EOF
)"
```

---

### Task 2: W64LABEL numbers a token line instead of swallowing it

**Files:**
- Modify: `src/conv/renumber.c` (`take_label`, around lines 629–654)
- Modify: `tools/test_renumber.c` (replace the `kw label` expect, around lines 293–294)
- Modify: `AGENTS.md` (the W64LABEL paragraph that begins "A letter starts a label")
- Modify: `docs/Todo-List.md` (the open bas2seq item under "bas2seq Bugs")
- Test: `tools/test_renumber.c`

**Interfaces:**
- Consumes: `conv_keyword_len(const unsigned char *s, unsigned int len, unsigned int i)` from `include/w64tools.h`. `take_label` is static. `labels_to_numbered` and `labels_to_numbered_store` both call it.
- Produces: `take_label` returns 0 when `conv_keyword_len(s, len, start) == (int)n` after the colon check. Return values stay 1 (label), 0 (not), and -1 (longer than 11 and ends with `:`). Mode 0 in `build_label_line` already writes the new number, one space, and the original line. A token line takes that path.

- [ ] **Step 1: Write the failing test**

In `tools/test_renumber.c`, delete the `kw label` call:

```c
    expect_labels("kw label", "PRINT: END\rGOTO PRINT\rIF X THEN PRINT \"Z\"",
                  "10 END\r20 GOTO 10\r30 IF X THEN PRINT \"Z\"", 10);
```

Put these calls in its place, still after `expect_labels("kept", ...)`:

```c
    expect_labels("kw stmt", "PRINT: END\rIF X THEN PRINT \"Z\"\r",
                  "10 PRINT: END\r20 IF X THEN PRINT \"Z\"\r", 10);
    expect_labels("kw twice", "PRINT: PRINT \"A\"\rPRINT: PRINT \"B\"\r",
                  "10 PRINT: PRINT \"A\"\r20 PRINT: PRINT \"B\"\r", 10);
    expect_labels("kw blank", "PRINT:\rEND\r",
                  "10 PRINT:\r20 END\r", 10);
    expect_labels("fn stmt", "SGN: PRINT \"X\"\r",
                  "10 SGN: PRINT \"X\"\r", 10);
    expect_labels("near kw", "PRINT1: END\rGOTO PRINT1\r",
                  "10 END\r20 GOTO 10\r", 10);
    expect_labels("kw indent", "  PRINT: END\r",
                  "10   PRINT: END\r", 10);
    expect_label_err("goto keyword", "GOTO PRINT\r", 8, 10, 6, 1);
```

`kw blank` is the line `W64LABEL` used to drop: a token with nothing after the colon was a label-only line, and the next code line wore its number. `kw indent` keeps the two spaces already on the line, then mode 0 inserts the usual space after the number, so the text starts with three spaces. `near kw` is still a real label. `goto keyword` is code 6 on row 1, and the buffer stays unchanged.

- [ ] **Step 2: Run the test to verify it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c && /tmp/test_renumber`

Expected: `FAIL kw stmt` with got text `10 END\r20 IF X THEN PRINT "Z"\r`. The program then prints `FAIL` for `kw twice` (rc=8), `kw blank`, `fn stmt`, and `kw indent`. `near kw` and the older label expects still pass. The process exits 1.

- [ ] **Step 3: Compare the name length with `conv_keyword_len`**

In `src/conv/renumber.c`, change the colon check inside `take_label` from:

```c
    if (i >= len || s[i] != ':')
        return 0;
    if (n > 11)
        return -1;
```

to:

```c
    if (i >= len || s[i] != ':')
        return 0;
    if (conv_keyword_len(s, len, *start) == (int)n)
        return 0;
    if (n > 11)
        return -1;
```

`*start` is already the first character of the name. The length compare is required: `conv_keyword_len` on `PRINT1:` returns 5, and the name length is 6, so `PRINT1` remains a label. `GOTO` is before `GO` in the token table, so `GOTO:` matches `GOTO` (length 4) and `GO:` matches `GO` (length 2). Do not copy the keyword array into this file. Do not call `conv_keyword_len` again from `labels_to_numbered` or `labels_to_numbered_store`.

Update the comment above `take_label` to:

```c
/* 1 label, 0 not, -1 the name is longer than 11 and ends with ':'.
   An exact V2 token is 0. The length has to match conv_keyword_len. */
```

In `AGENTS.md`, add this sentence immediately after "A letter starts a label, then letters or digits, at most 11, then `:`.":

```markdown
A name that is exactly one uppercase entry in the token table in `src/conv/token.c` is not a label. `PRINT:` and `SGN:` stay statements. `PRINT1:` is still a label. `print:` is still a label.
```

In `docs/Todo-List.md`, replace the open item that starts `PRINT is being detected as a label` with:

```markdown
- [x] PRINT is being detected as a label. No BASIC functions/tokens should be getting detected as labels. If the w64label program detects this as a label it will also need to be fixed as well.
    A name that is exactly an uppercase V2 token is not a label in `bas2seq` or `W64LABEL`. `PRINT:` and `SGN:` stay statements, and a second `PRINT:` is not `duplicate label`. `PRINT1:` is still a label. `print:` is still a label. Host checks passed. Not run on the Ultimate.
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c && /tmp/test_renumber`

Expected: no `FAIL` line. The process exits 0. `then print`, `forgoto`, `remember`, and `under` still pass. Then run `python3 tools/test_bas2seq.py` again. Expected: every check prints `PASS`.

- [ ] **Step 5: Commit**

```bash
git add src/conv/renumber.c tools/test_renumber.c AGENTS.md docs/Todo-List.md
git commit -m "$(cat <<'EOF'
Number a BASIC token line in W64LABEL instead of taking it as a label.

PRINT: was stored as a label, so the blank-line PRINT was dropped and a second copy was DUP LABEL.
EOF
)"
```
