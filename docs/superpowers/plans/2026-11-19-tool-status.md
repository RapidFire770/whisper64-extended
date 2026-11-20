# Converter stage text and drive screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The three conversion programs name the stage on the progress row, and they clear the menu before asking for the read and write drives.

**Architecture:** `conv_set_progress` in `src/conv/renumber.c` passes the stage label with the row. `tool_progress_line` in `src/conv/toolui.c` paints that label on the existing progress row. Disk reads call it with their own label. `tool_prepare` calls the existing `draw_title` before the drive questions. The editor is not linked to any of this.

**Tech Stack:** C, LLVM-MOS SDK `c64` platform, host `gcc` for `tools/test_renumber.c`. The three tools stay `-Oz`.

## Global Constraints

- The editor is unchanged. No new source file is added.
- `__heap_start` stays at or below `$C800` on `w64conv`, `w64renum`, and `w64label`. `__stack` stays `$D000`.
- The progress text uses the same row today's `LINE n` uses. `LINE LONG` and `NO LABEL` stay error text.
- The label passed to the callback includes the colon and the trailing space. The painter appends the number.
- `tool_note_progress` still calls `cbm_k_clrch`. Do not add `cbm_k_chkin` inside it. A disk read that already calls `cbm_k_chkin(2)` after the paint keeps that call.
- `renumber_seq` and `labels_to_numbered` do not call the progress function. A null pointer paints nothing.
- String literals live next to the walk that emits them. Do not add a table of every phrase.
- `seq_store` paints `TOKENIZING LINE: ` for its one walk, including when strip is on.
- Rows do not move. The read drive is row 3. The write drive is row 4. `USE 8-15` stays red on row 5.
- `tools/test_w64conv_c64u.py` is not run. It resets the Ultimate. A VICE run is not a substitute for the Ultimate check.
- Do not commit `.superpowers/`.

## File structure

- `include/w64tools.h` declares `conv_set_progress`. The callback gains the label.
- `src/conv/renumber.c` owns the store-walk labels and calls the callback.
- `include/w64toolui.h` and `src/conv/toolui.c` own the painter and the drive screen. `tool_prepare` calls `draw_title`, which is already static in that file.
- `src/conv/conv_main.c`, `src/conv/renum_main.c`, and `src/conv/label_main.c` pass the disk-read labels.
- `tools/test_renumber.c` records the label and the row. No new test file.
- `AGENTS.md`, `README.md`, and `docs/Todo-List.md` match the spec's Docs section. They are updated in Task 3, after the screen check.

---

### Task 1: Name the stage on the progress row

**Files:**
- Modify: `include/w64tools.h`
- Modify: `src/conv/renumber.c`
- Modify: `include/w64toolui.h`
- Modify: `src/conv/toolui.c`
- Modify: `src/conv/conv_main.c`
- Modify: `src/conv/renum_main.c`
- Modify: `src/conv/label_main.c`
- Test: `tools/test_renumber.c`

**Interfaces:**
- Consumes: the existing `note_row` call after each `row++` in the five store walks
- Produces: `void conv_set_progress(void (*fn)(const char *label, unsigned int row))` and `void tool_progress_line(const char *label, unsigned int n)`. A null function pointer paints nothing. `tool_progress_line` paints `label` plus the decimal number on the row from `tool_set_progress_row`.

- [ ] **Step 1: Write the failing test**

Replace `progress_rows`, `grab_progress`, and `expect_store_progress` in `tools/test_renumber.c` with the block below. In `main`, leave the `expect_store_progress()` call where it is and add the two new calls immediately after it.

```c
static const char *progress_labels[8];
static unsigned int progress_rows[8];
static int progress_n;

static void grab_progress(const char *label, unsigned int row) {
    if (progress_n < 8) {
        progress_labels[progress_n] = label;
        progress_rows[progress_n] = row;
        progress_n++;
    }
}

static void expect_progress(const char *what, int rc, int err, int n,
                            const char **want, const unsigned int *rows) {
    int i;

    if (rc != 0 || err != 0 || progress_n != n) {
        printf("FAIL %s progress rc=%d err=%d n=%d\n", what, rc, err, progress_n);
        failed = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        if (progress_rows[i] != rows[i] ||
            progress_labels[i] == 0 ||
            strcmp(progress_labels[i], want[i]) != 0) {
            printf("FAIL %s progress %d got %s %u\n", what, i,
                   progress_labels[i] ? progress_labels[i] : "(null)",
                   progress_rows[i]);
            failed = 1;
        }
    }
}

static void expect_store_progress(void) {
    const char *in = "10 END\r20 END";
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    unsigned long ceiling = 0;
    int err = 99;
    int row = 77;
    int rc;
    static const char *want[] = {
        "PASS 1 LINE: ", "PASS 1 LINE: ",
        "PASS 2 LINE: ", "PASS 2 LINE: ",
        "NUMBERING LABELED LINES: ", "NUMBERING LABELED LINES: "
    };
    static const unsigned int rows[] = { 1, 2, 1, 2, 1, 2 };

    memset(reu_mem, 0, sizeof reu_mem);
    memcpy(reu_mem, in, strlen(in));
    progress_n = 0;
    conv_set_progress(grab_progress);
    rc = labels_to_numbered_store(strlen(in), sizeof reu_mem, 1,
                                  &out_at, &out_len, &err, &row, &ceiling);
    conv_set_progress(0);
    expect_progress("label", rc, err, 6, want, rows);
}

static void expect_renumber_progress(void) {
    const char *in = "10 END\r20 END";
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    int err = 99;
    int row = 77;
    int rc;
    static const char *want[] = {
        "PASS 1 LINE: ", "PASS 1 LINE: ",
        "PASS 2 LINE: ", "PASS 2 LINE: ",
        "RENUMBERING LINE: ", "RENUMBERING LINE: "
    };
    static const unsigned int rows[] = { 1, 2, 1, 2, 1, 2 };

    memset(reu_mem, 0, sizeof reu_mem);
    memcpy(reu_mem, in, strlen(in));
    progress_n = 0;
    conv_set_progress(grab_progress);
    rc = renumber_seq_store(strlen(in), sizeof reu_mem, 10,
                            &out_at, &out_len, &err, &row);
    conv_set_progress(0);
    expect_progress("renumber", rc, err, 6, want, rows);
}

static void expect_tokenize_progress(void) {
    const char *in = "10 END\r20 END";
    unsigned long out_at = 0;
    unsigned long out_len = 0;
    int wrote = 0;
    int err = 99;
    int row = 77;
    int rc;
    static const char *want[] = { "TOKENIZING LINE: ", "TOKENIZING LINE: " };
    static const unsigned int rows[] = { 1, 2 };

    memset(reu_mem, 0, sizeof reu_mem);
    memcpy(reu_mem, in, strlen(in));
    progress_n = 0;
    conv_set_progress(grab_progress);
    rc = tokenize_store(0, strlen(in), sizeof reu_mem,
                        &out_at, &out_len, &wrote, &err, &row);
    conv_set_progress(0);
    expect_progress("tokenize", rc, err, 2, want, rows);
}
```

`main` calls them in this order, still before `expect_text`:

```c
    expect_store_progress();
    expect_renumber_progress();
    expect_tokenize_progress();
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c`

Expected: compile error. `grab_progress` does not match `conv_set_progress`, which still takes `void (*)(unsigned int)`.

- [ ] **Step 3: Pass the label from each store walk**

In `include/w64tools.h`, replace the `conv_set_progress` declaration with:

```c
void conv_set_progress(void (*fn)(const char *label, unsigned int row));
```

In `src/conv/renumber.c`, replace the function pointer, `conv_set_progress`, and `note_row` with:

```c
static void (*progress_fn)(const char *label, unsigned int row);

void conv_set_progress(void (*fn)(const char *label, unsigned int row)) {
    progress_fn = fn;
}

static void note_row(const char *label, unsigned long row) {
    unsigned int n;

    if (!progress_fn)
        return;
    n = row > 65535ul ? 65535u : (unsigned int)row;
    progress_fn(label, n);
}
```

Change only the five `note_row(row)` calls. Do not call `note_row` from `renumber_seq` or `labels_to_numbered`.

| Location | Replacement |
|---|---|
| `renumber_seq_store` map build, after `row++` | `note_row("PASS 1 LINE: ", row);` |
| `renumber_seq_store` inside `for (pass = 0; pass < 2; pass++)`, after `row++` | `note_row(pass == 0 ? "PASS 2 LINE: " : "RENUMBERING LINE: ", row);` |
| `labels_to_numbered_store` collection, after `row++` | `note_row("PASS 1 LINE: ", row);` |
| `labels_to_numbered_store` inside its `for (pass = 0; pass < 2; pass++)`, after `row++` | `note_row(pass == 0 ? "PASS 2 LINE: " : "NUMBERING LABELED LINES: ", row);` |
| `tokenize_store`, after `row++` | `note_row("TOKENIZING LINE: ", row);` |

- [ ] **Step 4: Run the host tests**

Run:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c && /tmp/test_renumber
gcc -Wall -Wextra -Iinclude -o /tmp/test_bigfile tools/test_bigfile.c src/conv/renumber.c src/conv/token.c && /tmp/test_bigfile
gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c && /tmp/test_format_line
python3 tools/test_basic_prg.py
```

Expected: each program exits 0. A line that starts with `FAIL` means stop.

- [ ] **Step 5: Paint the label, including the disk reads**

In `include/w64toolui.h`, replace the `tool_progress_line` declaration with:

```c
void tool_progress_line(const char *label, unsigned int n);
```

In `src/conv/toolui.c`, replace `tool_progress_line` and the buffer in `tool_note_progress`:

```c
void tool_progress_line(const char *label, unsigned int n) {
    tool_note_progress(progress_at, label, n);
}

void tool_note_progress(unsigned char row, const char *label, unsigned int n) {
    char msg[40];

    cbm_k_clrch();
    tool_set_str(msg, label);
    tool_add_dec(msg, n);
    tool_put_row(row, msg);
}
```

`cbm_k_clrch` stays. Do not add `cbm_k_chkin` here.

Replace each disk-read call. Leave the `cbm_k_chkin(2)` that already follows a call.

| File | Old call | New call |
|---|---|---|
| `src/conv/conv_main.c` `seq_store` | `tool_progress_line(seen);` | `tool_progress_line("TOKENIZING LINE: ", seen);` |
| `src/conv/conv_main.c` `prg_store` | `tool_progress_line(line_no);` | `tool_progress_line("PRG LINE: ", line_no);` |
| `src/conv/renum_main.c` `read_seq_reu` | `tool_progress_line(lines);` | `tool_progress_line("READING LINE: ", lines);` |
| `src/conv/renum_main.c` `prg_to_text` | `tool_progress_line(line_no);` | `tool_progress_line("PRG LINE: ", line_no);` |
| `src/conv/label_main.c` `read_seq_reu` | `tool_progress_line(lines);` | `tool_progress_line("READING LINE: ", lines);` |
| `src/conv/label_main.c` `read_seq_stripped` | `tool_progress_line(seen);` | `tool_progress_line("STRIPPING LINE: ", seen);` |

In `stash_seq_lines` in `src/conv/conv_main.c`, replace the single `tool_progress_line(seen);` with:

```c
        if (strip)
            tool_progress_line("STRIPPING LINE: ", seen);
        else
            tool_progress_line("READING LINE: ", seen);
```

The later `if (strip) conv_strip_rem(line);` stays. `conv_set_progress(tool_progress_line);` in each `main` stays. The signatures now match.

- [ ] **Step 6: Build the three programs and check the heap**

Run, from the repo root, using the existing `build` directory:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/josh/Documents/Programming/C64/tools/llvm-mos
cmake --build build
```

Expected: `build/whisper64.prg`, `build/w64conv.prg`, `build/w64renum.prg`, and `build/w64label.prg` link.

Then:

```bash
grep -E '__heap_start|__stack' build/w64conv.map build/w64renum.map build/w64label.map
```

Expected: each `__stack` is `$D000`. Each `__heap_start` is at or below `$C800`. Record the three heap addresses for Task 3. The editor map is not part of this check.

- [ ] **Step 7: Commit**

```bash
git add include/w64tools.h include/w64toolui.h src/conv/renumber.c src/conv/toolui.c \
  src/conv/conv_main.c src/conv/renum_main.c src/conv/label_main.c tools/test_renumber.c
git commit -m "$(cat <<'EOF'
Name the stage on the converter progress row.

EOF
)"
```

---

### Task 2: Clear the screen before the drive questions

**Files:**
- Modify: `src/conv/toolui.c` (`tool_prepare`)

**Interfaces:**
- Consumes: `draw_title` in `src/conv/toolui.c`, already defined above `tool_prepare`. It clears the screen, copies the 8 title bytes at columns 16–23 of row 0, and paints the REU line.
- Produces: `tool_prepare` calls `draw_title` after `tool_rows_reset` and before the first `tool_ask_drive`. Row numbers are unchanged.

- [ ] **Step 1: Call draw_title before the questions**

`tool_prepare` is only reached from the three menus, while row 0 still holds the program name. In `src/conv/toolui.c`, change the start of `tool_prepare` to:

```c
int tool_prepare(char want) {
    char msg[40];

    tool_rows_reset();
    draw_title();
    if (tool_ask_drive(tool_take_row(), "READ DRIVE (8-15): ", &read_drive) < 0)
        return -1;
    if (tool_ask_drive(tool_take_row(), "WRITE DRIVE (8-15): ", &write_drive) < 0)
        return -1;
```

The rest of `tool_prepare` stays. `tool_pick_file` still calls `draw_title` again. `paint_drive` still restores rows 3 and 4 after a file is chosen. Do not add a prototype. `draw_title` is already static above this function.

- [ ] **Step 2: Rebuild**

Run: `cmake --build build`

Expected: the three tool PRGs link. `__heap_start` on each map is still at or below `$C800`, and `__stack` is still `$D000`. If a heap address moved, keep the new value for Task 3.

- [ ] **Step 3: Commit**

```bash
git add src/conv/toolui.c
git commit -m "$(cat <<'EOF'
Clear the menu before the converter asks for a drive.

EOF
)"
```

---

### Task 3: Confirm the screen, then update the docs

**Files:**
- Modify: `AGENTS.md`
- Modify: `README.md`
- Modify: `docs/Todo-List.md`

**Interfaces:**
- Consumes: the labels from Task 1 and `draw_title` from Task 2
- Produces: the docs described in the spec. The two Todo items are checked only after the Ultimate screen matches.

- [ ] **Step 1: See if the Ultimate answers**

Run: `c64u info`

Expected: device identity. If the command fails, stop and ask Josh to bring the Ultimate online. Do not run `./run_vice.sh`. Do not run `tools/test_w64conv_c64u.py`.

- [ ] **Step 2: Check the drive screen**

The REU probe runs before the menu. Poll for the menu before sending a key.

```bash
c64u machine reset
c64u runners run-prg-upload build/w64renum.prg
```

Poll until row 3 contains `1 RENUMBER SEQ`, or 30 seconds pass. If row 12 says `REU 128KB REQUIRED`, stop. This check needs the 16MB REU.

```bash
c64u machine read-mem 0400 --to 07e7 --raw > /tmp/w64screen.bin
python3 - <<'PY'
from pathlib import Path
b = Path("/tmp/w64screen.bin").read_bytes()
row = b[120:160].decode("latin1")
print(repr(row))
raise SystemExit(0 if row.startswith("1 RENUMBER SEQ") else 1)
PY
```

Then:

```bash
c64u machine sendkey '1'
```

Poll again until row 3 starts with `READ DRIVE (8-15):`. Then require all of these, and stop if any fail:

- Row 0, from column 16, is `W64RENUM`.
- Row 1 contains `REU:`.
- Row 3 starts with `READ DRIVE (8-15):`.
- Row 4 does not contain `2 RENUMBER PRG`.
- Row 5 does not contain `? HELP`.

Row `n` is bytes `n * 40` through `n * 40 + 39` of `/tmp/w64screen.bin`. The tools store those letters as themselves.

Leave this run here. Step 3 continues it. Do not reset between Step 2 and Step 3.

- [ ] **Step 3: Check one stage name**

Continue the `W64RENUM` run from Step 2. Send the drives, take the first SEQ, and write a new name:

```bash
c64u machine sendkey '8\n'
c64u machine sendkey '8\n'
c64u machine sendkey '\n'
c64u machine sendkey 'ZSTATUS\n'
c64u machine sendkey '\n'
```

After each key, read `$0400`–`$07E7` the same way as Step 2.

- If the screen contains `NO FILES`, stop and ask which disk has a SEQ. Do not invent a file.
- If the screen contains `OVERWRITE?`, send `N` and stop. Do not replace an existing file. Ask Josh for a free output name.
- Otherwise poll until the screen contains one of `READING LINE:`, `PASS 1 LINE:`, `PASS 2 LINE:`, or `RENUMBERING LINE:`. One of those four is the pass. A screen that only shows `LINE ` without those words fails.

`ZSTATUS` is the output of this check. Leave it on drive 8 and say so in the commit message of this task only if the job finished and wrote it.

- [ ] **Step 4: Update AGENTS.md**

In the `W64CONV`, `W64RENUM`, and `W64LABEL` section, replace the sentence that starts `The read drive is row 3` through the sentence `A null pointer paints nothing.` with:

```text
The read drive is row 3 and the write drive is row 4. `tool_prepare` calls `draw_title` before those questions, while the menu title is still on row 0, so the menu is gone first. `USE 8-15` is red on row 5. The file list is rows 6–23. After the list, those rows are blanked, the drive numbers are painted again, and `OUTPUT FILENAME:` is row 6. `BAD NAME` is the next row. `OVERWRITE?`, the progress row, the result, and `ANOTHER?` each take the next row. The progress row is repainted in place. A SEQ read paints `READING LINE: n`, or `STRIPPING LINE: n` when REM is stripped. A PRG read paints `PRG LINE: n` with the BASIC line number. The first store walk paints `PASS 1 LINE: n`. The measure walk paints `PASS 2 LINE: n`. The renumber write paints `RENUMBERING LINE: n`. The label write paints `NUMBERING LABELED LINES: n`. Tokenizing paints `TOKENIZING LINE: n`. `seq_store` paints `TOKENIZING LINE: n` for its one walk, including when strip is on. The label includes the colon and the space. `conv_set_progress` points the store walks at that paint. A null pointer paints nothing.
```

If Task 1 or Task 2 moved `__heap_start`, replace the three current-link addresses in that same section with the addresses from the maps. `__stack` stays `$D000`. If the addresses are still `$9033`, `$8D85`, and `$A2D2`, leave that sentence.

- [ ] **Step 5: Update README.md**

Replace the paragraph that begins `A long job paints` and the sentence that begins `Each one asks for a read drive` with:

```markdown
A long job paints the stage and the line on one row. A SEQ read paints `READING LINE: n`, or `STRIPPING LINE: n` when REM is stripped. A PRG read paints `PRG LINE: n` with the BASIC line number. The next walks paint `PASS 1 LINE: n`, `PASS 2 LINE: n`, then `RENUMBERING LINE: n` or `NUMBERING LABELED LINES: n`, and a PRG write paints `TOKENIZING LINE: n`. `n` starts over when the next walk starts, except `PRG LINE:`. Keeping the SEQ numbers paints only `TOKENIZING LINE: n`. All three tools treat an LF as a line break. A CR followed by an LF is an empty line, the same split the editor uses.

Each one clears the screen, leaves the title and the REU line, and then asks for a read drive and a write drive, 8–15. An empty entry is 8. The editor's F2 save stays a SEQ. N or Stop at the another-file prompt leaves the tool. `?` opens help, and Stop leaves that page. RUN/STOP on the main menu exits.
```

- [ ] **Step 6: Check off the todo items**

In `docs/Todo-List.md`, change the line-status item and the drive-screen item from `- [ ]` to `- [x]`. Leave the other open items open.

- [ ] **Step 7: Commit**

```bash
git add AGENTS.md README.md docs/Todo-List.md
git commit -m "$(cat <<'EOF'
Document the converter stage text and the cleared drive screen.

EOF
)"
```

If Step 3 wrote `ZSTATUS`, add that fact to the commit message body.
