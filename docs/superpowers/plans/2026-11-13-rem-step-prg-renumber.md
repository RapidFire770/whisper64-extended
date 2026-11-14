# REM strip, line step, and PRG renumber Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `W64CONV` and `W64LABEL` can strip `REM` comments and take a line step when writing a PRG, and `W64RENUM` can renumber a PRG while keeping the bytes that follow the BASIC program.

**Architecture:** `conv_strip_rem` edits one SEQ line in place with the same quote, `DATA`, and `REM` scan as `conv_tokenize`. A typed step stores that text in the REU and calls the existing `renumber_seq_store`, then `tokenize_store`. An empty step on `W64CONV` keeps today's streaming tokenize. The PRG job detokenizes with `conv_format_line`, parks the machine-language tail at the top of the REU, renumbers below it, and writes the tail back onto the PRG. `README.md` is updated in the same work, including editor facts that are already true and missing from that file.

**Tech Stack:** C, LLVM-MOS SDK, host `gcc` checks. No new source file. The editor is not linked into this work.

## Global Constraints

- `conv_strip_rem(char *line)` rewrites one NUL-terminated line of at most 79 characters. The scan matches `conv_tokenize`. A quote toggles string mode. `DATA` copies until a colon outside quotes. A quote check runs before the `DATA` check. Outside quotes and `DATA`, `conv_keyword_len` recognizes `REM`. `REMEMBER` stays. Lowercase `rem` stays. `REM` inside quotes or `DATA` stays.
- The match consumes `REM` and the rest of the line. The introducing colon and the spaces around it come off. The colon stays when the remainder is optional spaces, a letter, letters or digits, then that colon, and the name is not an exact token from `src/conv/token.c`. A name longer than 11 still keeps the colon.
- `10` after a strip is a numbered line with no statement. `start:` is a bare label. An empty result is a blank line. `10 IF A THEN REM NOTE` becomes `10 IF A THEN` and the conversion still succeeds.
- `STRIP REM? Y/N` is row 18. `Y` and `y` strip. `N` and `n` leave comments. Any other key waits. Stop sets `verify_code` the way `tool_read_field` does, and the caller uses `tool_another`.
- `STEP:` is row 21, an 8-byte field. Return on an empty field keeps SEQ numbers in `W64CONV` and means 10 in `W64LABEL` and `W64RENUM`. A typed 0 is passed to the renumber and comes back `NUM BIG`. A non-digit or a value past 63999 is `NUM BIG` from `tool_parse_step`. A failure writes nothing.
- `W64CONV` menu `2` is unchanged, including `ML 1: EXTRA BYTES NOT COPIED` and colon truncation. `W64RENUM` does not ask about `REM`.
- The PRG job refuses a load address other than `01 08` (`NOT BASIC PRG`), a broken link (`BAD BASIC PRG`), and a line number past 63999 (`NUM BIG`). `conv_format_line` returning a negative length or a set truncated flag is `LINE LONG` plus that BASIC line number. The shortened line is not stored.
- The CHRIN byte that arrives with STATUS bit 6 is part of the tail. A following synthetic CR is not, the same rule as `tool_trailing_ml`. The tail is parked at `reu_bytes - tail_len`. Both store calls receive that address as the ceiling. A zero-length tail is not moved.
- `__heap_start` for `w64conv`, `w64renum`, and `w64label` stays at or below `$C800`. `__stack` stays `$D000`. The three tools stay `-Oz`. Do not compile them at `-Os`. `--gc-sections` stays. Do not add a RAM file buffer. Do not raise the directory list past 144 entries.
- `src/help.c` is not edited. Help page 2 stays the short strings.
- Do not run `tools/test_w64conv_c64u.py`. It resets the Ultimate.
- Host checks, from the repo root: `gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c && /tmp/test_format_line`, `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c && /tmp/test_renumber`, and `gcc -Wall -Wextra -Iinclude -o /tmp/test_bigfile tools/test_bigfile.c src/conv/renumber.c src/conv/token.c && /tmp/test_bigfile`.

## File map

| File | Responsibility |
|---|---|
| `include/w64tools.h` | Declares `conv_strip_rem` |
| `src/conv/token.c` | `conv_strip_rem` |
| `tools/test_format_line.c` | One host check per strip row |
| `tools/test_renumber.c` | A bare `10` still holds a number. A stripped `start: REM menu` numbers like a bare label |
| `include/w64toolui.h` | Declares `tool_ask_yn` |
| `src/conv/toolui.c` | `Y` / `N` / Stop wait |
| `src/conv/label_main.c` | Strip question, step question, line reader when the answer is Y, help rows |
| `src/conv/conv_main.c` | Strip on the stream. Typed step stores the SEQ, renumbers, tokenizes |
| `CMakeLists.txt` | `w64conv.prg` links `src/conv/renumber.c`. `w64renum.prg` links `src/conv/token.c` |
| `src/conv/renum_main.c` | Menu `2`, detokenize, park the tail, renumber, write the PRG |
| `README.md` | Tool behavior after this work, plus the editor facts already missing |
| `AGENTS.md` | Step, strip, PRG job, heap addresses from the new maps |
| `docs/Todo-List.md` | Check off the three `W64TOOLS` items |

---

### Task 1: Strip one line

**Files:**
- Modify: `include/w64tools.h` (next to `conv_tokenize`)
- Modify: `src/conv/token.c` (after `conv_tokenize`, before `conv_format_line`)
- Modify: `tools/test_format_line.c`
- Test: `tools/test_format_line.c`

**Interfaces:**
- Consumes: `conv_keyword_len`
- Produces: `void conv_strip_rem(char *line);` The buffer is rewritten in place and stays NUL-terminated. An empty result is `line[0] == 0`.

- [ ] **Step 1: Write the failing test**

In `tools/test_format_line.c`, add this helper above `main`:

```c
static void expect_strip(const char *name, const char *src, const char *want) {
    char buf[80];

    memset(buf, 0xA5, sizeof buf);
    memcpy(buf, src, strlen(src) + 1);
    conv_strip_rem(buf);
    if (strcmp(buf, want) != 0) {
        printf("FAIL %s\n  got  [%s]\n  want [%s]\n", name, buf, want);
        failed++;
    }
}
```

At the start of `main`, before `expect_src("short", ...)`, call:

```c
    expect_strip("colon space", "10 PRINT \"HI\": REM NOTE", "10 PRINT \"HI\"");
    expect_strip("colon tight", "10 PRINT \"HI\":REM NOTE", "10 PRINT \"HI\"");
    expect_strip("only rem", "10 REM NOTE", "10");
    expect_strip("rem eats rest", "10 REM:PRINT \"HI\"", "10");
    expect_strip("label", "start: REM menu", "start:");
    expect_strip("label code", "start:PRINT \"HI\":REM bye", "start:PRINT \"HI\"");
    expect_strip("quoted", "10 PRINT \"REM\"", "10 PRINT \"REM\"");
    expect_strip("data word", "10 DATA REM", "10 DATA REM");
    expect_strip("data then rem", "10 DATA \"HI\":REM NOTE", "10 DATA \"HI\"");
    expect_strip("remember", "10 REMEMBER", "10 REMEMBER");
    expect_strip("lower", "10 rem note", "10 rem note");
    expect_strip("then rem", "10 IF A THEN REM NOTE", "10 IF A THEN");
    expect_strip("no number", "REM NOTE", "");
    expect_strip("long label", "PRINT1: REM X", "PRINT1:");
    expect_strip("keyword colon", "PRINT: REM X", "PRINT");
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c && /tmp/test_format_line`

Expected: compile fails with `conv_strip_rem` undeclared, or the link fails with an undefined reference.

- [ ] **Step 3: Implement the strip**

In `include/w64tools.h`, after the `conv_tokenize` declaration:

```c
/* Drops a real REM and the rest of the line. Rewrites line in place. */
void conv_strip_rem(char *line);
```

In `src/conv/token.c`, after `conv_tokenize` and before `conv_format_line`:

```c
static int strip_letter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int strip_name(unsigned char c) {
    return strip_letter(c) || (c >= '0' && c <= '9');
}

/* 1 when the colon at colon_at is the label colon for this head. */
static int strip_label_colon(const unsigned char *s, unsigned int colon_at) {
    unsigned int i = 0;
    unsigned int start;
    unsigned int n = 0;

    while (i < colon_at && s[i] == ' ')
        i++;
    if (i >= colon_at || !strip_letter(s[i]))
        return 0;
    start = i;
    while (i < colon_at && strip_name(s[i])) {
        n++;
        i++;
    }
    if (n == 0 || i != colon_at || s[i] != ':')
        return 0;
    if (conv_keyword_len(s, colon_at + 1, start) == (int)n)
        return 0;
    return 1;
}

void conv_strip_rem(char *line) {
    unsigned char *s = (unsigned char *)line;
    unsigned int len = 0;
    unsigned int i = 0;
    int quote = 0;
    int data = 0;

    while (s[len])
        len++;
    while (i < len) {
        int n;

        if (quote) {
            if (s[i] == '"')
                quote = 0;
            i++;
            continue;
        }
        if (s[i] == '"') {
            quote = 1;
            i++;
            continue;
        }
        if (data) {
            if (s[i] == ':')
                data = 0;
            i++;
            continue;
        }
        n = conv_keyword_len(s, len, i);
        if (n == 3 && s[i] == 'R' && s[i + 1] == 'E' && s[i + 2] == 'M')
            break;
        if (n == 4 && s[i] == 'D' && s[i + 1] == 'A' &&
            s[i + 2] == 'T' && s[i + 3] == 'A') {
            data = 1;
            i = i + 4;
            continue;
        }
        i++;
    }
    if (i >= len)
        return;
    while (i > 0 && s[i - 1] == ' ')
        i--;
    if (i > 0 && s[i - 1] == ':') {
        if (!strip_label_colon(s, i - 1)) {
            i--;
            while (i > 0 && s[i - 1] == ' ')
                i--;
        }
    }
    s[i] = 0;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c && /tmp/test_format_line`

Expected: `ok`

- [ ] **Step 5: Commit**

```bash
git add include/w64tools.h src/conv/token.c tools/test_format_line.c
git commit -m "$(cat <<'EOF'
Strip a real REM from one SEQ line before it is numbered or tokenized.

EOF
)"
```

---

### Task 2: A stripped line still numbers

**Files:**
- Modify: `tools/test_renumber.c` (inside `main`, after the `"rem"` check)
- Test: `tools/test_renumber.c`

**Interfaces:**
- Consumes: `conv_strip_rem` from Task 1, `renumber_seq`, `labels_to_numbered`
- Produces: no new function. The existing renumber keeps a line whose text is only a number, and a bare `start:` still shares the next code line.

- [ ] **Step 1: Write the failing test**

These pass against today's renumber. Add them anyway so a later edit cannot drop the bare line. In `main`, after `expect_text("rem", ...)`:

```c
    expect_text("bare line", "10\r20 GOTO 10\r", "5\r10 GOTO 5\r", 5);
    {
        char joined[80];
        char parts[3][40];
        int p;

        strcpy(parts[0], "start: REM menu");
        strcpy(parts[1], "PRINT \"HI\"");
        strcpy(parts[2], "GOTO start");
        joined[0] = 0;
        for (p = 0; p < 3; p++) {
            conv_strip_rem(parts[p]);
            if (p)
                strcat(joined, "\r");
            strcat(joined, parts[p]);
        }
        expect_labels("stripped label", joined, "10 PRINT \"HI\"\r20 GOTO 10", 10);
    }
```

`expect_labels` is already in this file. `string.h` is already included.

- [ ] **Step 2: Run the test**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c && /tmp/test_renumber`

Expected: `ok` is not printed by this file. It returns 0 and prints nothing on success. A failing check prints `FAIL` and the process exits non-zero. Confirm the exit status is 0.

- [ ] **Step 3: Commit**

```bash
git add tools/test_renumber.c
git commit -m "$(cat <<'EOF'
Cover a bare numbered line and a REM label once the comment is gone.

EOF
)"
```

There is no production change in this task. The checks lock the numbering `conv_strip_rem` hands to the existing functions.

---

### Task 3: W64LABEL asks, then strips

**Files:**
- Modify: `include/w64toolui.h`
- Modify: `src/conv/toolui.c` (after `tool_confirm_overwrite`)
- Modify: `src/conv/label_main.c`
- Test: host checks from Task 1 and Task 2, then the `w64label.prg` link

**Interfaces:**
- Consumes: `conv_strip_rem`, `tool_parse_step`, `labels_to_numbered_store`, `tokenize_store`
- Produces: `int tool_ask_yn(unsigned char row, const char *label);` Returns 1 for Y, 0 for N, and -1 for Stop after setting `verify_code` to `CONV_CANCEL`.

- [ ] **Step 1: Add the yes/no wait**

In `include/w64toolui.h`, after `tool_confirm_overwrite`:

```c
int tool_ask_yn(unsigned char row, const char *label);
```

In `src/conv/toolui.c`, after `tool_confirm_overwrite`:

```c
int tool_ask_yn(unsigned char row, const char *label) {
    tool_put_row(row, label);
    for (;;) {
        int ch = tool_cgetc();

        if (ch == 3) {
            verify_code = CONV_CANCEL;
            return -1;
        }
        if (ch == 'Y' || ch == 'y')
            return 1;
        if (ch == 'N' || ch == 'n')
            return 0;
    }
}
```

- [ ] **Step 2: Read lines when the user strips**

In `src/conv/label_main.c`, add this next to `read_seq_reu`. It is used only when the answer is Y. N keeps `read_seq_reu`.

```c
static int read_seq_stripped(void) {
    char spec[28];
    unsigned char eof = 0;
    char line[80];

    tool_file_spec(spec, 0, in_name, ",S,R");
    if (tool_open_ch(2, read_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    tool_stash_reset();
    for (;;) {
        int err = 0;
        int rc;
        int i;

        cbm_k_chkin(2);
        rc = tool_read_seq_line(&eof, line, &err);
        if (rc < 0) {
            if (err == -2) {
                char msg[12];

                tool_set_str(msg, "READ ");
                tool_add_hex2(msg, last_st);
                tool_finish(CONV_DISK, msg);
            } else
                tool_finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        if (rc > 0)
            break;
        conv_strip_rem(line);
        for (i = 0; line[i]; i++) {
            if (tool_stash_byte((unsigned char)line[i]) < 0)
                return -1;
        }
        if (tool_stash_byte(13) < 0)
            return -1;
    }
    tool_stash_flush();
    tool_close_ch(2);
    return 0;
}
```

Change `label_job` to take the two answers:

```c
static void label_job(int strip, unsigned int step) {
```

Replace the `read_seq_reu()` call with:

```c
    if (strip) {
        if (read_seq_stripped() < 0)
            return;
    } else if (read_seq_reu() < 0)
        return;
```

Replace the hardcoded `10` in the `labels_to_numbered_store` call with `step`.

- [ ] **Step 3: Ask, and update the help rows**

In `show_help`, change row 4 from `STEP IS 10` to `STEP EMPTY IS 10`. Add:

```c
    tool_put_row(6, "Y STRIPS REM AND KEEPS THE LINE");
```

In `main`, after a successful `tool_prepare('S')` and before `label_job()`:

```c
        {
            int strip;
            char buf[8];
            unsigned int step;

            strip = tool_ask_yn(18, "STRIP REM? Y/N");
            if (strip < 0) {
                if (verify_code != 0)
                    tool_another();
                continue;
            }
            if (tool_read_field(21, "STEP: ", buf, 8) < 0) {
                if (verify_code != 0)
                    tool_another();
                continue;
            }
            if (tool_parse_step(buf, &step) < 0) {
                tool_finish(CONV_NUM_BIG, "NUM BIG");
                tool_another();
                continue;
            }
            label_job(strip, step);
        }
```

Remove the old bare `label_job();` so it is not called twice.

- [ ] **Step 4: Build and check the heap**

```bash
cd /home/josh/Documents/Programming/C64/whisper64-extended/build
cmake -DCMAKE_PREFIX_PATH=/home/josh/Documents/Programming/C64/tools/llvm-mos ..
make w64label.prg
python3 - <<'PY'
from pathlib import Path
text = Path("w64label.map").read_text(errors="replace")
heap = stack = None
for line in text.splitlines():
    parts = line.replace("=", " ").split()
    if "__heap_start" in parts:
        heap = next(p for p in parts if p != "__heap_start")
    if "__stack" in parts:
        stack = next(p for p in parts if p != "__stack")
print("heap", heap, "stack", stack)
assert stack.lower().endswith("d000")
assert int(heap, 16) <= 0xC800
PY
```

Expected: `w64label.prg` links, the script prints the two addresses, and the assert does not fire. If `__heap_start` is above `$C800`, stop. Do not add another buffer to get it under.

Also rerun the two host commands from Task 1 and Task 2. Expected: `ok` from the format test, and exit 0 from the renumber test.

- [ ] **Step 5: Commit**

```bash
git add include/w64toolui.h src/conv/toolui.c src/conv/label_main.c
git commit -m "$(cat <<'EOF'
Ask W64LABEL whether to strip REM and what step to number with.

EOF
)"
```

---

### Task 4: W64CONV keeps numbers unless a step is typed

**Files:**
- Modify: `CMakeLists.txt` (`w64conv.prg` source list)
- Modify: `src/conv/conv_main.c`
- Test: `w64conv.prg` link, plus the host checks

**Interfaces:**
- Consumes: `conv_strip_rem`, `tool_ask_yn`, `tool_parse_step`, `renumber_seq_store`, `tokenize_store`
- Produces: no new exported function. Menu 1 takes the two answers. An empty step still calls `seq_store`. A typed step stores the SEQ at REU address 0 and tokenizes the renumbered text. The ceiling passed to `tokenize_store` is `reu_bytes`.

- [ ] **Step 1: Link the renumber**

In `CMakeLists.txt`, add `src/conv/renumber.c` to `add_w64_tool(w64conv.prg ...)` after `src/conv/token.c`. Do not add `labels_to_numbered` calls. `--gc-sections` drops the label entry points when nothing calls them.

- [ ] **Step 2: Strip on the stream, and store lines for a typed step**

Change the signature to `static int seq_store(unsigned int *wrote_out, int strip)`. After a successful `tool_read_seq_line` and before `conv_tokenize`:

```c
        if (strip)
            conv_strip_rem(line);
```

Add this reader. It always splits on CR or LF, because `renumber_seq_store` splits on CR only. `strip` selects the REM edit.

```c
static int stash_seq_lines(int strip) {
    char spec[28];
    unsigned char eof = 0;
    char line[80];
    unsigned int seen = 0;

    tool_file_spec(spec, 0, in_name, ",S,R");
    if (tool_open_ch(2, read_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    tool_stash_reset();
    for (;;) {
        int err = 0;
        int rc;
        int i;

        cbm_k_chkin(2);
        rc = tool_read_seq_line(&eof, line, &err);
        if (rc < 0) {
            if (err == -2) {
                char msg[12];

                tool_set_str(msg, "READ ");
                tool_add_hex2(msg, last_st);
                tool_finish(CONV_DISK, msg);
            } else
                tool_finish(CONV_LINE_LONG, "LINE LONG");
            return -1;
        }
        if (rc > 0)
            break;
        if (seen < 65535u)
            seen++;
        tool_note_progress(10, "SEQ ", seen);
        if (strip)
            conv_strip_rem(line);
        for (i = 0; line[i]; i++) {
            if (tool_stash_byte((unsigned char)line[i]) < 0)
                return -1;
        }
        if (tool_stash_byte(13) < 0)
            return -1;
    }
    tool_stash_flush();
    tool_close_ch(2);
    return 0;
}
```

`tool_note_progress` calls `cbm_k_clrch`, so the next iteration must `cbm_k_chkin(2)` before the read. The loop above already does that.

Change `seq_to_prg` to:

```c
static void seq_to_prg(int strip, int keep, unsigned int step) {
    char probe[28];
    char spec[28];
    unsigned int stream_wrote = 0;
    int wrote = 0;
    int replace;
    int err = 0;
    int row = 0;
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    unsigned long prg_at = 0;
    unsigned long prg_len = 0;
    unsigned long text_len;

    tool_file_spec(probe, 0, out_name, ",P,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (keep) {
        if (seq_store(&stream_wrote, strip) < 0)
            return;
        wrote = (int)stream_wrote;
        tool_file_spec(spec, replace, out_name, ",P,W");
        if (tool_flush_at(spec, 0, tool_stored()) < 0)
            return;
    } else {
        if (stash_seq_lines(strip) < 0)
            return;
        text_len = tool_stored();
        if (text_len &&
            renumber_seq_store(text_len, reu_bytes, step, &out_at, &out_len,
                               &err, &row)) {
            tool_show_err(err, row);
            return;
        }
        if (tokenize_store(out_at, out_len, reu_bytes, &prg_at, &prg_len,
                           &wrote, &err, &row)) {
            tool_show_err(err, row);
            return;
        }
        tool_file_spec(spec, replace, out_name, ",P,W");
        if (tool_flush_at(spec, prg_at, prg_len) < 0)
            return;
    }
    if (!tool_drive_status_ok())
        return;
    if (wrote == 0)
        tool_put_row(12, "EMPTY PRG");
    else {
        char msg[20];
        tool_set_str(msg, "WROTE ");
        tool_add_dec(msg, (unsigned int)wrote);
        tool_put_row(12, msg);
    }
    tool_file_spec(probe, 0, out_name, ",P,R");
    tool_remember_output(probe);
}
```

`seq_store` today ends with `tool_stash_flush` and `tool_close_ch(2)`, and the keep path flushes from address 0. Keep that. `tokenize_store` takes `int *wrote`. `seq_store` takes `unsigned int *`. The two variables above stay separate.

`renumber_seq_store` does not set `out_at` when the text length is 0. The `if (text_len &&` guard skips that call and leaves `out_at` and `out_len` at 0. `tokenize_store` still runs and writes the four-byte empty program `01 08 00 00`.

- [ ] **Step 3: Ask on menu 1, and add the help lines**

In `show_help`, add:

```c
    tool_put_row(6, "STEP EMPTY KEEPS NUMBERS");
    tool_put_row(20, "Y STRIPS REM AND KEEPS THE LINE");
```

Leave row 19 as `ML 1:        EXTRA BYTES NOT COPIED`.

In `main`, replace `seq_to_prg();` with the question block. Menu 2 still calls `prg_to_seq()` with no new questions.

```c
        if (mode == 1) {
            int strip;
            char buf[8];
            unsigned int step = 0;
            int keep;

            strip = tool_ask_yn(18, "STRIP REM? Y/N");
            if (strip < 0) {
                if (verify_code != 0)
                    tool_another();
                continue;
            }
            if (tool_read_field(21, "STEP: ", buf, 8) < 0) {
                if (verify_code != 0)
                    tool_another();
                continue;
            }
            keep = buf[0] == '\0';
            if (!keep && tool_parse_step(buf, &step) < 0) {
                tool_finish(CONV_NUM_BIG, "NUM BIG");
                tool_another();
                continue;
            }
            seq_to_prg(strip, keep, step);
        } else
            prg_to_seq();
```

A typed 0 parses as success and is passed through as `step`. `renumber_seq_store` returns 1, and `tool_show_err` prints `NUM BIG`.

- [ ] **Step 4: Build and check the heap**

```bash
cd /home/josh/Documents/Programming/C64/whisper64-extended/build
cmake -DCMAKE_PREFIX_PATH=/home/josh/Documents/Programming/C64/tools/llvm-mos ..
make w64conv.prg
python3 - <<'PY'
from pathlib import Path
text = Path("w64conv.map").read_text(errors="replace")
heap = stack = None
for line in text.splitlines():
    parts = line.replace("=", " ").split()
    if "__heap_start" in parts:
        heap = next(p for p in parts if p != "__heap_start")
    if "__stack" in parts:
        stack = next(p for p in parts if p != "__stack")
print("heap", heap, "stack", stack)
assert stack.lower().endswith("d000")
assert int(heap, 16) <= 0xC800
PY
```

Expected: link succeeds and the assert does not fire. Rerun the three host commands from the Global Constraints. Expected: format test prints `ok`, and the other two exit 0.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt src/conv/conv_main.c
git commit -m "$(cat <<'EOF'
Let W64CONV strip REM and renumber when a step is typed.

EOF
)"
```

---

### Task 5: W64RENUM renumbers a PRG and keeps the tail

**Files:**
- Modify: `CMakeLists.txt` (`w64renum.prg` source list)
- Modify: `src/conv/renum_main.c`
- Test: `w64renum.prg` link and the host checks

**Interfaces:**
- Consumes: `conv_format_line`, `renumber_seq_store`, `tokenize_store`, `tool_parse_step`, `tool_flush_at`, `w64_store_get`, `w64_store_put`
- Produces: menu key `2` runs the PRG job. The written file is the tokenized program followed by the tail bytes. Choice 1 is the existing SEQ job.

- [ ] **Step 1: Link the tokenizer**

In `CMakeLists.txt`, add `src/conv/token.c` to `add_w64_tool(w64renum.prg ...)` after `src/conv/renumber.c`.

- [ ] **Step 2: Slide bytes in the REU**

At the top of `src/conv/renum_main.c`, after the includes (`w64tools.h` is already included; add `w64reu.h` if the include list does not have it):

```c
static void reu_slide(unsigned long dst, unsigned long src, unsigned long n,
                      int from_high) {
    unsigned char chunk[256];
    unsigned long off;

    if (n == 0 || dst == src)
        return;
    if (from_high) {
        off = n;
        while (off) {
            unsigned int take = 256;

            if (off < take)
                take = (unsigned int)off;
            off -= take;
            w64_store_get(src + off, chunk, take);
            w64_store_put(dst + off, chunk, take);
        }
    } else {
        off = 0;
        while (off < n) {
            unsigned int take = 256;

            if (n - off < take)
                take = (unsigned int)(n - off);
            w64_store_get(src + off, chunk, take);
            w64_store_put(dst + off, chunk, take);
            off += take;
        }
    }
}
```

- [ ] **Step 3: Read the PRG into text plus a tail**

Add `prg_to_text`. It returns 0 and writes `*text_len` and `*tail_len`, or -1 after `tool_finish`. The text sits at REU address 0. The tail sits at `text_len` until the caller moves it.

```c
static int put_tail_byte(unsigned long at, int b) {
    unsigned char c;

    if (at >= reu_bytes) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    c = (unsigned char)b;
    w64_store_put(at, &c, 1);
    return 0;
}

static int copy_tail(unsigned char *eof, unsigned long at, unsigned long *tail_len) {
    int b;
    int b2;
    unsigned long n = 0;

    *tail_len = 0;
    if (*eof)
        return 0;
    b = tool_read_byte(2, eof);
    if (b < 0)
        return 0;
    if (b == 13 && *eof) {
        b2 = tool_read_byte(2, eof);
        if (b2 < 0)
            return 0;
        if (put_tail_byte(at + n, b) < 0)
            return -1;
        n++;
        b = b2;
    }
    for (;;) {
        if (put_tail_byte(at + n, b) < 0)
            return -1;
        n++;
        if (*eof)
            break;
        b = tool_read_byte(2, eof);
        if (b == -2) {
            char msg[12];

            tool_set_str(msg, "READ ");
            tool_add_hex2(msg, last_st);
            tool_finish(CONV_DISK, msg);
            return -1;
        }
        if (b < 0)
            break;
    }
    *tail_len = n;
    return 0;
}

static int prg_to_text(unsigned long *text_len, unsigned long *tail_len) {
    char spec[28];
    unsigned char eof = 0;
    unsigned int addr = 0x0801;
    unsigned int link;
    unsigned int line_no;
    int lo, hi;
    unsigned int seen = 0;

    tool_file_spec(spec, 0, in_name, ",P,R");
    if (tool_open_ch(2, read_drive, spec) != 0) {
        tool_finish(CONV_OPEN_FAIL, "OPEN FAIL");
        return -1;
    }
    cbm_k_chkin(2);
    tool_stash_reset();
    lo = tool_read_byte(2, &eof);
    hi = tool_read_byte(2, &eof);
    if (lo != 0x01 || hi != 0x08) {
        tool_finish(CONV_NOT_BASIC, "NOT BASIC PRG");
        return -1;
    }
    for (;;) {
        int remain;
        int slen = 0;
        int b;

        cbm_k_chkin(2);
        lo = tool_read_byte(2, &eof);
        hi = tool_read_byte(2, &eof);
        if (lo < 0 || hi < 0) {
            tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        link = (unsigned int)(lo | (hi << 8));
        if (link == 0)
            break;
        if (link <= addr || (unsigned int)(link - addr) < 5 ||
            (unsigned int)(link - addr) > 256) {
            tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        lo = tool_read_byte(2, &eof);
        hi = tool_read_byte(2, &eof);
        if (lo < 0 || hi < 0) {
            tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
            return -1;
        }
        line_no = (unsigned int)(lo | (hi << 8));
        if (line_no > 63999u) {
            tool_finish(CONV_NUM_BIG, "NUM BIG");
            return -1;
        }
        remain = (int)(link - addr) - 4;
        slen = 0;
        {
            int cut_stmt = 0;

            while (remain > 1) {
                b = tool_read_byte(2, &eof);
                if (b < 0) {
                    tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
                    return -1;
                }
                if (slen < 79)
                    tok[slen++] = (unsigned char)b;
                else
                    cut_stmt = 1;
                remain--;
            }
            b = tool_read_byte(2, &eof);
            if (b != 0) {
                tool_finish(CONV_BAD_BASIC, "BAD BASIC PRG");
                return -1;
            }
            {
                int cut = 0;
                int len = conv_format_line(line_no, tok, slen, text_line, 80, &cut);
                int j;

                if (len < 0 || cut || cut_stmt) {
                    char msg[20];

                    tool_set_str(msg, "LINE LONG ");
                    tool_add_dec(msg, line_no);
                    tool_finish(CONV_LINE_LONG, msg);
                    return -1;
                }
                for (j = 0; j < len; j++) {
                    if (tool_stash_byte((unsigned char)text_line[j]) < 0)
                        return -1;
                }
                if (tool_stash_byte(13) < 0)
                    return -1;
            }
        }
        addr = link;
        if (seen < 65535u)
            seen++;
        tool_note_progress(10, "PRG ", seen);
    }
    tool_stash_flush();
    *text_len = tool_stored();
    if (copy_tail(&eof, *text_len, tail_len) < 0)
        return -1;
    tool_close_ch(2);
    if (*text_len + *tail_len < *text_len) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return -1;
    }
    return 0;
}
```

`conv_format_line` writes the line number into `text_line`. The CR is stored after that text. A negative length, a set `cut`, or token bytes past the 79 kept in `tok` (`cut_stmt`) is `LINE LONG` and that BASIC line number. The shortened line mode 2 would write is not stored.

- [ ] **Step 4: Renumber and write**

```c
static void renumber_prg(unsigned int step) {
    char probe[28];
    char spec[28];
    int replace;
    int err = 0;
    int row = 0;
    int wrote = 0;
    unsigned long text_len = 0;
    unsigned long tail_len = 0;
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    unsigned long prg_at = 0;
    unsigned long prg_len = 0;
    unsigned long ceiling;

    tool_file_spec(probe, 0, out_name, ",P,R");
    replace = tool_confirm_overwrite(probe);
    if (!replace)
        return;
    if (prg_to_text(&text_len, &tail_len) < 0)
        return;
    if (text_len + tail_len > reu_bytes) {
        tool_finish(CONV_TOO_BIG, "TOO BIG");
        return;
    }
    ceiling = reu_bytes - tail_len;
    if (tail_len)
        reu_slide(ceiling, text_len, tail_len, 1);
    if (text_len) {
        if (renumber_seq_store(text_len, ceiling, step, &out_at, &out_len,
                               &err, &row)) {
            tool_show_err(err, row);
            return;
        }
    }
    if (tokenize_store(out_at, out_len, ceiling, &prg_at, &prg_len,
                       &wrote, &err, &row)) {
        tool_show_err(err, row);
        return;
    }
    if (tail_len)
        reu_slide(prg_at + prg_len, ceiling, tail_len, 0);
    tool_file_spec(spec, replace, out_name, ",P,W");
    if (tool_flush_at(spec, prg_at, prg_len + tail_len) < 0)
        return;
    if (!tool_drive_status_ok())
        return;
    tool_put_row(12, "RENUMBERED");
    tool_file_spec(probe, 0, out_name, ",P,R");
    tool_remember_output(probe);
}
```

The first slide copies from the high end because the tail moves upward. The second copies from the low end because the tail moves down to the byte after the PRG. `from_high` is 1, then 0.

- [ ] **Step 5: Menu and help**

Replace `draw_menu` body rows with:

```c
    tool_put_row(3, "1 RENUMBER SEQ");
    tool_put_row(4, "2 RENUMBER PRG");
    tool_put_row(5, "? HELP");
    tool_put_row(6, "RUN/STOP EXITS THE PROGRAM");
```

Replace the top of `show_help` through the help-key line with:

```c
    tool_put_row(3, "1. SEQ - RENUMBERS LINES IN SEQ");
    tool_put_row(4, "2. PRG - RENUMBERS A PRG, KEEPS ML");
    tool_put_row(5, "STEP EMPTY IS 10");
    tool_put_row(6, "?  HELP - SHOWS THIS HELP SCREEN");
```

Leave the error rows 7–14 and row 23 as they are.

In `menu_key`, replace the `'1'` test with:

```c
        if (ch == '1' || ch == '2')
            return ch - '0';
```

Stop still returns -1.

In `main`, `tool_prepare` takes `'S'` for mode 1 and `'P'` for mode 2. The existing step question stays, then:

```c
            if (mode == 1)
                renumber_job(step);
            else
                renumber_prg(step);
```

- [ ] **Step 6: Build and check the heap**

```bash
cd /home/josh/Documents/Programming/C64/whisper64-extended/build
cmake -DCMAKE_PREFIX_PATH=/home/josh/Documents/Programming/C64/tools/llvm-mos ..
make w64renum.prg
python3 - <<'PY'
from pathlib import Path
text = Path("w64renum.map").read_text(errors="replace")
heap = stack = None
for line in text.splitlines():
    parts = line.replace("=", " ").split()
    if "__heap_start" in parts:
        heap = next(p for p in parts if p != "__heap_start")
    if "__stack" in parts:
        stack = next(p for p in parts if p != "__stack")
print("heap", heap, "stack", stack)
assert stack.lower().endswith("d000")
assert int(heap, 16) <= 0xC800
PY
```

Expected: link succeeds and the assert does not fire. Rerun the three host commands. Expected: format test prints `ok`, and the other two exit 0.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/conv/renum_main.c
git commit -m "$(cat <<'EOF'
Renumber a PRG in W64RENUM and copy the bytes that follow BASIC.

EOF
)"
```

---

### Task 6: README, AGENTS.md, and the todo list

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`
- Modify: `docs/Todo-List.md`
- Test: the heap script below, read against the spec's Docs section

**Interfaces:**
- Consumes: the three maps from Tasks 3–5
- Produces: the user-facing description and the agent notes match the programs

- [ ] **Step 1: Replace the tool paragraph in the README**

In `README.md`, replace the paragraph that starts `` `W64CONV` menu `1` is SEQ to PRG `` with these three paragraphs:

```markdown
`W64CONV` menu `1` reads a numbered SEQ and writes a PRG. It asks `STRIP REM? Y/N`. `Y` removes a real `REM` and the rest of that line. A `REM` inside quotes stays, and so does `REMEMBER`. A line that is only a REM keeps its line number. It then asks `STEP:`. Return keeps the numbers in the SEQ. A number from 1 to 63999 renumbers and rewrites `GOTO`, `GOSUB`, `THEN`, `ELSE`, `GO TO`, and `GO SUB`. Menu `2` reads a PRG and writes a SEQ. A long line is cut back to the last whole statement, and the status line counts those cuts as `TRUNC`. Bytes after the BASIC program are not copied (`ML 1`).

`W64RENUM` menu `1` renumbers a SEQ and writes a SEQ. Menu `2` renumbers a PRG and writes a PRG. Menu `2` copies the bytes that follow the BASIC program. Both ask `STEP:`. An empty step is 10.

`W64LABEL` reads a labeled SEQ and writes a PRG. It asks `STRIP REM? Y/N` with the same rules. `start: REM menu` keeps the label. It asks `STEP:`. An empty step is 10.
```

In the sentences that follow, keep drive 8–15, the empty entry of 8, F2 staying SEQ, and N or Stop at the another-file prompt. Add: `` `?` opens help, and Stop leaves that page. RUN/STOP on the main menu exits. ``

- [ ] **Step 2: Fill the editor facts the README already skipped**

Add these rows to the key table, in key order with the other CTRL rows:

```markdown
| **CTRL+B** | Previous page                 |
| **CTRL+N** | Next page                     |
```

Change the Goto row to: `Goto a file-wide line. The title-bar number is 1-based. A page is 48 lines.`

In Features, add:

```markdown
- **Display wrap**: A line longer than the screen continues on the next row. Return starts a new editor line
- **Help**: F8 opens two pages. Right shows page 2, Left shows page 1, Stop or Return leaves
```

In BASIC Mode, add:

```markdown
- In BASIC mode the 80th character is not stored. The status row says `BASIC LINE FULL`
```

After the three-tool bullets, add:

```markdown
### bas2seq

`tools/bas2seq` runs on the desktop. From `tools/`, `python3 -m bas2seq` reads a PC `.bas` file and writes a SEQ the editor can open. It does not number lines. `W64LABEL` still does that on the C64.
```

Do not edit `src/help.c`.

- [ ] **Step 3: Update AGENTS.md and check off the todo**

In `AGENTS.md`, replace this sentence:

`W64RENUM` renumbers a whole SEQ. The step prompt defaults to 10 when left empty. `W64LABEL` numbers labeled lines at step 10 and writes a PRG.

with:

`W64RENUM` renumbers a SEQ or a PRG. The step prompt defaults to 10 when left empty. Menu 2 copies the bytes that follow the BASIC program onto the new PRG. `W64LABEL` asks `STRIP REM? Y/N`, then a step. An empty step is 10. `Y` strips before the labels are numbered. `W64CONV` menu 1 asks the same strip question, then a step. An empty step keeps the SEQ numbers. A typed step renumbers before the PRG is written.

Replace this sentence:

`W64CONV` menu `1` is SEQ to PRG and `2` is PRG to SEQ. `W64RENUM` renumbers a SEQ. An empty step is 10. `W64LABEL` numbers labels at step 10 and writes a PRG, and it does not ask for a step.

with:

`W64CONV` menu `1` is SEQ to PRG. It asks `STRIP REM? Y/N` on row 18 and `STEP:` on row 21. An empty step keeps the SEQ numbers. Menu `2` is PRG to SEQ. `W64RENUM` menu `1` renumbers a SEQ and menu `2` renumbers a PRG. An empty step is 10. Menu `2` lists PRG files and copies the tail. `W64LABEL` lists SEQ, asks the strip question and the step, and an empty step is 10.

In that same paragraph, replace `W64RENUM` and `W64LABEL` list SEQ with: `W64RENUM` lists SEQ for menu 1 and PRG for menu 2. `W64LABEL` lists SEQ.

Record `__heap_start` from `build/w64conv.map`, `build/w64renum.map`, and `build/w64label.map` next to the existing October 24, 2026 figures, and say those three are the current link. Leave `__stack` at `$D000`.

In `docs/Todo-List.md`, check off the three open items at the REM strip, the line step, and the PRG renumber. Leave the "large job looks frozen" item open. Leave the bas2seq comment items open.

- [ ] **Step 4: Confirm the three heaps once**

```bash
cd /home/josh/Documents/Programming/C64/whisper64-extended/build
python3 - <<'PY'
from pathlib import Path
for name in ("w64conv", "w64renum", "w64label"):
    text = Path(name + ".map").read_text(errors="replace")
    heap = stack = None
    for line in text.splitlines():
        parts = line.replace("=", " ").split()
        if "__heap_start" in parts:
            heap = next(p for p in parts if p != "__heap_start")
        if "__stack" in parts:
            stack = next(p for p in parts if p != "__stack")
    print(name, "heap", heap, "stack", stack)
    assert stack.lower().endswith("d000")
    assert int(heap, 16) <= 0xC800
PY
```

Expected: three lines, each heap at or below `c800`, each stack ending in `d000`.

- [ ] **Step 5: Commit**

```bash
git add README.md AGENTS.md docs/Todo-List.md
git commit -m "$(cat <<'EOF'
Document REM stripping, the line step, and PRG renumber, and catch the README up.

EOF
)"
```

The editor sources are not in this commit.
