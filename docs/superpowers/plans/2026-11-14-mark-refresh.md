# Mark Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mark-mode arrows repaint only the lines the selection moved across, a marked BASIC keyword turns yellow, and paste draws the new text once.

**Architecture:** `refresh_move` already skips `redraw_screen` for a normal arrow. The mark branch is the exception: it calls `update_cursor`. That branch instead calls `draw_editor_band` for the old cursor line and the new one. Keyword paint already runs after the yellow mark; yellow takes the higher rank, and the 40-column keyword pass skips a marked column. `paste_clipboard` already changes the buffer through `insert_char` and `new_line`; it calls `update_cursor` once after the loop, then `show_message`, because `redraw_screen` clears row 24. The directory loop stops calling `clrscr` on every key and repaints the two rows whose selection changed.

**Tech Stack:** C, LLVM-MOS SDK (`c64` platform), `tools/c64u.py` against the C64 Ultimate at `http://192.168.100.193`.

## Global Constraints

- Editor link: `__bss_end` must stay at or below `$CC00`. `__stack` stays `$D000`. The current link has `__bss_end` at `$CBB0`, `__heap_start` at `$CBD0`, and `__stack` at `$D000`. Stop if a rebuild puts `__bss_end` past `$CC00`.
- `shift_rows_40`, `shift_rows_80`, and `shift_viewport` in `src/screen.c` stay `noinline`. Inlining them put `__bss_end` at `$CC32`.
- Do not divide or take a remainder by `edit_width`. It is 37 or 77. Compare twice, the way `text_rows_for` does.
- Do not grow `lines[]`, the undo buffers, or the clipboard. The clipboard is `clipboard[2][MAX_LINE_LENGTH]`. Copy stops at 2 lines. Do not raise that to 8.
- Do not call the KERNAL between `screen80_begin_draw` and `screen80_end_draw`. Nested begin/end is reference-counted. A later begin after `end_draw` is a new window.
- The status row is row 24. `redraw_screen` clears it. Mark arrows and the directory cursor must not.
- 80-column text goes through `draw_slice` / `render_line_80`. `tools/c64u.py` reads `$0400` only. It cannot see 80-column bitmap mode.
- Do not claim a C64 behavior works unless VICE or the Ultimate showed it. If the Ultimate at `http://192.168.100.193` is unreachable, pause and ask Josh to bring it back online.
- Build the editor with `cmake --build build --target whisper64` from the repo root. Do not compile `w64conv`, `w64renum`, or `w64label` at `-Os`.
- These editor paths stay on `update_cursor`: startup, CTRL+D, F4, CTRL+N, CTRL+B, Return, delete at column 0, a page change from Up or Down, HOME, help exit, load, save, new file, goto, search, replace, undo, and redo. Each one replaces the window or the title. A phantom-row change inside `refresh_move` stays on `update_cursor` too.
- `tool_pick_file` in `src/conv/toolui.c` repaints its 18 rows in place and does not `clrscr` per key. Leave it alone. This plan is the editor.

---

## Root cause

`src/screen.c` `refresh_move` does this when `mark_active` is set:

```c
if (mark_active) {
    update_cursor();
    return;
}
```

`update_cursor` calls `redraw_screen`, which blanks the title, walks every editor line, and clears row 24. A normal arrow already takes `paint_cursor_only` or `shift_viewport`. Mark mode never reaches them. `main.c` updates `mark_end_x` and `mark_end_y` to the new cursor before `refresh_move`, and the previous end is `old_x` / `old_y`. One arrow changes the mark on at most the old line and the new line.

Yellow is applied, then keywords are painted over it. In `cell_rank`, purple and blue return 3 and yellow returns 2, so an 80-column cell keeps the keyword. `paint_40_keyword` writes the keyword color with `cputc_at` after `draw_slice` has stored yellow. A marked `PRINT` stays purple. The selection is invisible on the word the user is most likely to mark.

`column_marked` gets a single-line range right: it takes the lesser and greater column. A multi-line range uses `mark_start_x` only when that line is `mark_start_y`, and otherwise treats the top line as marked from column 0 and the bottom line as marked through `len`. Marking upward paints both endpoint lines solid. `copy_marked` has the same branch, and its loop runs while `clipboard_lines < 8`. The array has 2 rows. A third line writes through `clipboard_lines` into `mark_active` and `basic_mode`.

`paste_clipboard` calls `insert_char` and `new_line` and then `show_message("TEXT PASTED")`. Nothing redraws the editor rows. The buffer has the bytes. `draw_cursor` in 40-column mode copies `lines[cursor_y][cursor_x]` onto the screen with the reverse bit, so walking the cursor shows the hidden text. Delete calls `update_current_line`. F4 calls `update_cursor`. Both make the line appear. `redraw_screen` clears row 24, so the redraw has to run before `show_message`. `mark_toggle` has the same order on the way out: it prints `MARK OFF` and then `update_cursor` wipes that row. Turning mark on never draws `[M]` until some later full redraw.

`show_directory` calls `clrscr` at the top of the key loop, then draws the title, the 18 names, and the footer. Up and Down redraw that whole screen. The load and cancel branches are fine.

No other editor key loop calls `redraw_screen` per repeat. Return, a line-join delete, page changes, HOME, search, undo, F4, and the mode toggle each call `update_cursor` once because the window contents changed.

## File map

| File | Responsibility |
|---|---|
| `tools/c64u.py` | 40-column Ultimate check `mark` |
| `src/screen.c` | Mark rank, upward range, band limited to the two lines |
| `src/clipboard.c` | Two-line copy, paste redraw, mark message after the redraw |
| `src/file_ops.c` | Directory chrome once, two-row selection update |
| `docs/Todo-List.md` | Check off the two editor bugs after the Ultimate check |
| `AGENTS.md` | Mark rank, mark arrows, directory paint, copy limit |
| `README.md` | One line on copy and marked keywords |

---

### Task 1: Failing Ultimate check for mark color and mark arrows

**Files:**
- Modify: `tools/c64u.py`
- Test: `python3 tools/c64u.py mark`

**Interfaces:**
- Consumes: `boot_editor`, `inject_keys`, `dump_rows`, `row_text`, `cells_changed`, `allowed_move`, `read_mem`, `SCREEN`, `COLOR`
- Produces: `cmd_mark()` and the `mark` command. Later tasks add a paste boot and a directory boot to this function.

- [ ] **Step 1: Add the key codes and the command**

Next to the other `K_` constants:

```python
K_CTRL_K = 0x0B
K_F1 = 0x85
```

`K_CTRL_C` is `3` and `K_CTRL_V` is `22`. Use the numeric literals at the call so this file does not grow a pile of aliases. `3` is also RUN/STOP. The editor treats `3` as copy when `cursor_y < num_lines`.

Add `cmd_mark` before `main`, and a branch in `main`:

```python
elif cmd == "mark":
    sys.exit(cmd_mark())
```

Update the docstring usage line to mention `python3 tools/c64u.py mark`.

```python
def cmd_mark():
    boot_editor()
    inject_keys([K_F4], settle=0.6)
    inject_keys(list(b"PRINT") + [K_RETURN] + list(b"XY") + [K_HOME], settle=0.8)
    inject_keys([K_CTRL_K], settle=0.5)
    before_s, before_c = dump_rows(label="mark on PRINT")
    if b"MARK" not in row_text(before_s, 24):
        print("FAIL mark status missing")
        return 1
    inject_keys([K_RIGHT] * 5, settle=0.6)
    after_s, after_c = dump_rows(label="marked PRINT")
    if row_text(after_s, 24) != row_text(before_s, 24):
        print("FAIL mark arrows cleared the status row")
        return 1
    if not allowed_move(cells_changed(before_s, after_s), {1}):
        print("FAIL mark arrows rewrote rows other than the PRINT line")
        return 1
    if not allowed_move(cells_changed(before_c, after_c), {1}):
        print("FAIL mark arrows rewrote colors outside the PRINT line")
        return 1
    for col in range(3, 8):
        if (after_c[40 + col] & 0x0F) != 7:
            print(f"FAIL PRINT column {col} is not yellow")
            return 1
    if plain_row(after_s, 2)[3:5] != b"XY":
        print("FAIL XY line changed while marking PRINT")
        return 1

    inject_keys([K_CTRL_K, K_DOWN, K_LEFT, K_CTRL_K], settle=0.8)
    before_s, before_c = dump_rows(label="anchor on Y")
    if b"MARK" not in row_text(before_s, 24):
        print("FAIL second mark status missing")
        return 1
    inject_keys([K_UP], settle=0.5)
    up_s, up_c = dump_rows(label="mark up onto R")
    if row_text(up_s, 24) != row_text(before_s, 24):
        print("FAIL upward mark cleared the status row")
        return 1
    if not allowed_move(cells_changed(before_s, up_s), {1, 2}):
        print("FAIL upward mark rewrote rows outside the two lines")
        return 1
    if (up_c[40 + 3] & 0x0F) != 4:
        print("FAIL unmarked P is not purple")
        return 1
    if (up_c[40 + 5] & 0x0F) != 7 or (up_c[40 + 6] & 0x0F) != 7:
        print("FAIL I or N is not yellow")
        return 1
    if (up_c[80 + 3] & 0x0F) != 7:
        print("FAIL X is not yellow")
        return 1
    if (up_c[80 + 4] & 0x0F) != 1:
        print("FAIL unmarked Y is not white")
        return 1
    print("ok")
    return 0
```

`plain_row` is already defined in this file. Screen column 3 is the first text column. `PRINT` is columns 3–7. After five rights the cursor sits on the space past `T`, so those five cells are the mark color and not the cursor. `XY` is editor line 1, screen row 2. The second `CTRL+K` is mark off (the first mark is still active), Down lands on `XY` with the column clamped to 2, Left lands on `Y`, and the third `CTRL+K` anchors there. Up moves to column 1 of `PRINT`, which is `R` and is under the cursor, so the check skips `R`. `P` stays purple. `I` and `N` are yellow. `X` is yellow. `Y` stays white. A whole-line upward mark would paint `P` and `Y` yellow.

- [ ] **Step 2: Run the check and confirm it fails**

Run: `python3 tools/c64u.py mark`

This resets the Ultimate, loads `build/whisper64.prg`, and drives it. Expected: a `FAIL` line. Today the five `PRINT` cells stay purple (4), and the status row is cleared by the first right. Either failure is the right result. Do not change `src/` in this task.

If the host is unreachable, stop and ask Josh.

- [ ] **Step 3: Commit**

```bash
git add tools/c64u.py
git commit -m "$(cat <<'EOF'
Add an Ultimate check for mark-mode redraw and keyword color.

EOF
)"
```

---

### Task 2: Yellow mark, upward range, and a two-line repaint

**Files:**
- Modify: `src/screen.c` (`column_marked`, `cell_rank`, `paint_40_keyword`, `draw_editor_band`, `refresh_move`)
- Modify: `src/clipboard.c` (`copy_marked`, `mark_toggle`)
- Test: `python3 tools/c64u.py mark`

**Interfaces:**
- Consumes: `draw_editor_band`, `erase_cursor`, `draw_title_pos`, `draw_cursor`, `shift_viewport`, `layout_changed`, `paint_cursor_only`
- Produces: `static void draw_editor_band(int first, int last, int line_lo, int line_hi)` and `static void repaint_mark_lines(int a, int b)`. Callers that paint every editor line pass `0, LINES_PER_PAGE`.

- [ ] **Step 1: Let yellow beat a keyword, and mark the real endpoint**

Replace `column_marked` with:

```c
static int column_marked(int line_num, int col, int len) {
    int start_y, end_y, x;

    (void)len;
    if (!mark_active)
        return 0;

    start_y = mark_start_y < mark_end_y ? mark_start_y : mark_end_y;
    end_y = mark_start_y < mark_end_y ? mark_end_y : mark_start_y;
    if (line_num < start_y || line_num > end_y)
        return 0;

    if (start_y == end_y) {
        int start_x = mark_start_x < mark_end_x ? mark_start_x : mark_end_x;
        int end_x = mark_start_x < mark_end_x ? mark_end_x : mark_start_x;
        return col >= start_x && col < end_x;
    }
    x = (line_num == mark_start_y) ? mark_start_x : mark_end_x;
    if (line_num == start_y)
        return col >= x;
    if (line_num == end_y)
        return col < x;
    return 1;
}
```

`len` stays in the signature so the call sites do not change. The cast keeps the parameter used.

Replace `cell_rank` so a marked cell stays yellow when the other glyph is a keyword:

```c
/* Two 4px glyphs share a hires cell. Mark yellow beats a keyword, which beats white. */
static uint8_t cell_rank(uint8_t color) {
    if (color == COL_YELLOW) return 3;
    if (color == COL_PURPLE || color == COL_BLUE) return 2;
    if (color == COL_WHITE) return 1;
    return 0;
}
```

`paint_line_colors` already paints yellow before the keyword pass. A higher yellow rank means the keyword pass does not replace that cell. An unmarked keyword cell is still purple or blue.

In `paint_40_keyword`, skip a marked column. The slice loop has already stored yellow there:

```c
        if (color[i] && i >= off && i < off + ew
            && !column_marked(line_num, i, len))
            cputc_at(3 + i - off, screen_row, lines[line_num][i], color[i]);
```

- [ ] **Step 2: Repaint the two editor lines instead of the window**

Change `draw_editor_band` to take the editor-line window and to draw a slice only when the line is inside it:

```c
static void draw_editor_band(int first, int last, int line_lo, int line_hi) {
```

The existing `if (row >= first && row <= last)` becomes:

```c
                if (row >= first && row <= last
                    && line >= line_lo && line <= line_hi) {
```

Do that in both the live-line branch and the empty-line branch. The three existing calls gain the full window:

```c
    draw_editor_band(1, EDIT_HEIGHT, 0, LINES_PER_PAGE);
```

```c
    draw_editor_band(first, last, 0, LINES_PER_PAGE);
```

```c
    draw_editor_band(first, last, 0, LINES_PER_PAGE);
```

Those are `redraw_screen`, `shift_viewport`, and `update_current_line`, in that order.

Add this above `refresh_move`:

```c
static void repaint_mark_lines(int a, int b) {
    int lo = a < b ? a : b;
    int hi = a < b ? b : a;

    if (screen_mode == MODE_80COL)
        screen80_begin_draw();
    erase_cursor();
    draw_title_pos();
    draw_editor_band(1, EDIT_HEIGHT, lo, hi);
    draw_cursor();
    if (screen_mode == MODE_80COL)
        screen80_end_draw();
}
```

Replace `refresh_move` with:

```c
void refresh_move(int old_x, int old_y, int old_scroll) {
    if (old_x == cursor_x && old_y == cursor_y && old_scroll == scroll_offset)
        return;
    fit_cursor();
    if (layout_changed(old_x, old_y)) {
        update_cursor();
        return;
    }
    if (scroll_offset != old_scroll) {
        if (!shift_viewport(old_scroll))
            update_cursor();
        else if (mark_active)
            repaint_mark_lines(old_y, cursor_y);
        return;
    }
    if (!mark_active) {
        paint_cursor_only();
        return;
    }
    repaint_mark_lines(old_y, cursor_y);
}
```

A same-line arrow repaints that one editor line, including its wrap rows. An up or down repaints those two lines and nothing under them. A scroll still shifts, then repaints the two lines because the line that was left behind still has the old mark extent. A phantom-row change still uses `update_cursor`. That path clears row 24. The Ultimate check uses short lines, so it does not take that path.

- [ ] **Step 3: Copy the same endpoints, and stop at two lines**

Replace the body of `copy_marked` from the `clipboard_lines = 0;` assignment through the loop. Keep the `mark_active` check and the final `show_message`.

```c
    clipboard_lines = 0;

    for (i = start_y; i <= end_y && clipboard_lines < 2; i++) {
        if (start_y == end_y) {
            int start_x = mark_start_x < mark_end_x ? mark_start_x : mark_end_x;
            int end_x = mark_start_x < mark_end_x ? mark_end_x : mark_start_x;
            strncpy(clipboard[clipboard_lines], &lines[i][start_x], end_x - start_x);
            clipboard[clipboard_lines][end_x - start_x] = '\0';
        } else if (i == start_y || i == end_y) {
            int x = (i == mark_start_y) ? mark_start_x : mark_end_x;
            if (i == start_y)
                strcpy(clipboard[clipboard_lines], &lines[i][x]);
            else {
                strncpy(clipboard[clipboard_lines], lines[i], x);
                clipboard[clipboard_lines][x] = '\0';
            }
        } else {
            strcpy(clipboard[clipboard_lines], lines[i]);
        }
        clipboard_lines++;
    }
```

The limit is 2 because `clipboard` has 2 rows. A longer mark keeps the first two of those lines.

Replace `mark_toggle` so both directions draw the window before the status text. The on path is what paints `[M]`. The off path clears the yellow. `show_message` runs after `update_cursor` so row 24 keeps the sentence.

```c
void mark_toggle() {
    if (!mark_active) {
        mark_active = 1;
        mark_start_x = cursor_x;
        mark_start_y = cursor_y;
        mark_end_x = cursor_x;
        mark_end_y = cursor_y;
        update_cursor();
        show_message("MARK ON - ARROWS, CTRL+C=COPY", COL_GREEN);
    } else {
        mark_active = 0;
        update_cursor();
        show_message("MARK OFF", COL_RED);
    }
}
```

- [ ] **Step 4: Build and read the map**

Run: `cmake --build build --target whisper64`

Then: `grep -E '__bss_end|__heap_start|__stack' build/whisper64.map`

`__bss_end` must be at or below `$CC00`. `__stack` stays `$D000`. If `__bss_end` is past `$CC00`, stop. Do not add more editor code. The band change is the piece to shrink: drop `draw_title_pos` only as a last resort, and do not put the mark path back on `redraw_screen`.

- [ ] **Step 5: Run the Ultimate check**

Run: `python3 tools/c64u.py mark`

Expected: `ok`. A `FAIL` on yellow means the keyword pass is still winning. A `FAIL` on the status row means an arrow still calls `redraw_screen`. A `FAIL` on `P` or `Y` means the upward endpoint is still column 0 or `len`.

- [ ] **Step 6: Commit**

```bash
git add src/screen.c src/clipboard.c
git commit -m "$(cat <<'EOF'
Repaint the two mark lines and keep a marked keyword yellow.

EOF
)"
```

---

### Task 3: Draw pasted text once

**Files:**
- Modify: `src/clipboard.c` (`paste_clipboard`)
- Modify: `tools/c64u.py` (`cmd_mark`)
- Test: `python3 tools/c64u.py mark`

**Interfaces:**
- Consumes: `update_cursor`, `show_message`, `insert_char`, `new_line`
- Produces: paste ends with `update_cursor()` and then `show_message("TEXT PASTED", COL_GREEN)`

- [ ] **Step 1: Extend the check so paste fails first**

In `cmd_mark`, replace the final `print("ok")` / `return 0` with a second boot. Leave the first boot's asserts as they are.

```python
    boot_editor()
    inject_keys(list(b"ABC") + [K_HOME, K_CTRL_K], settle=0.6)
    inject_keys([K_RIGHT, K_RIGHT, K_RIGHT, 3, 22], settle=0.8)
    scr, _ = dump_rows(label="pasted ABC")
    row = plain_row(scr, 1)
    if row[:9] != bytes([0x30, 0x30, 0x3A, 0x41, 0x42, 0x43, 0x41, 0x42, 0x43]):
        print("FAIL paste did not draw ABCABC")
        return 1
    if b"PASTED" not in row_text(scr, 24):
        print("FAIL paste status missing")
        return 1
    print("ok")
    return 0
```

`3` copies. `22` pastes. The cursor is on column 3, so the second `ABC` is inserted there. `plain_row` strips the reverse bit.

- [ ] **Step 2: Run the check and confirm the new assert fails**

Run: `python3 tools/c64u.py mark`

Expected: the first boot still prints nothing and the second prints `FAIL paste did not draw ABCABC`. The row is `00:ABC` and spaces. If the first boot fails, stop and fix Task 2 before touching paste.

- [ ] **Step 3: Redraw after the insert loop**

In `paste_clipboard`, after the loops and before the success message:

```c
    update_cursor();
    show_message("TEXT PASTED", COL_GREEN);
```

Remove the old `show_message("TEXT PASTED", COL_GREEN)` that sat above this. The empty-clipboard return stays where it is and does not redraw. One redraw covers a multi-line paste because `new_line` has already shifted the buffer. Do not call `update_current_line` per character.

- [ ] **Step 4: Run the check**

Run: `python3 tools/c64u.py mark`

Expected: `ok`.

- [ ] **Step 5: Commit**

```bash
git add src/clipboard.c tools/c64u.py
git commit -m "$(cat <<'EOF'
Redraw the editor once after a paste so the new text is visible.

EOF
)"
```

---

### Task 4: Stop clearing the directory on every arrow

**Files:**
- Modify: `src/file_ops.c` (`show_directory`)
- Modify: `tools/c64u.py` (`cmd_mark`)
- Test: `python3 tools/c64u.py mark`

**Interfaces:**
- Consumes: `cputc_at`, `cputs_at`, `dir_entries`, `num_dir_entries`, `disk_name`
- Produces: `static void draw_dir_row(int index, int selected, int scroll_pos)` and `static void draw_dir_rows(int selected, int scroll_pos)`. The KEY_RETURN load body and the `c == 3` cancel body stay the bytes they are now.

- [ ] **Step 1: Extend the check so the directory flash fails first**

Append this boot before `print("ok")` in `cmd_mark`. It needs a disk with at least two directory entries. `W64.d64` on the Ultimate has them.

```python
    boot_editor()
    inject_keys([K_F1], settle=0.4)
    scr = None
    deadline = time.time() + 8.0
    while time.time() < deadline:
        scr = read_mem(SCREEN, 1000)
        if b"DIRECTORY" in row_text(scr, 0) or b"NO FILES" in row_text(scr, 24):
            break
        time.sleep(0.25)
    if scr is None or b"DIRECTORY" not in row_text(scr, 0):
        print("FAIL directory did not open")
        return 1
    if row_text(scr, 5)[7] == 0x20:
        print("FAIL directory needs two files")
        return 1
    write_mem(SCREEN + 24 * 40, "5A")
    inject_keys([K_DOWN], settle=0.5)
    scr = read_mem(SCREEN, 1000)
    col = read_mem(COLOR, 1000)
    if scr[24 * 40] != 0x5A:
        print("FAIL directory arrow cleared the screen")
        return 1
    if b"DIRECTORY" not in row_text(scr, 0):
        print("FAIL directory title disappeared")
        return 1
    if (col[4 * 40] & 0x0F) != 1 or (col[5 * 40] & 0x0F) != 5:
        print("FAIL directory selection colors")
        return 1
    print("ok")
    return 0
```

The name in `"%4d  %-16s  %s"` starts at screen column 7, because the line is drawn at column 1. A short block count pads columns 1–3 with spaces, so column 1 is the wrong probe. Row 4 is the first file and starts selected (green 5). Down moves green to row 5 and leaves row 4 white (1). Row 24 is unused by the list. `clrscr` turns the sentinel back into a space. A row repaint leaves `0x5A`.

- [ ] **Step 2: Run the check and confirm the new assert fails**

Run: `python3 tools/c64u.py mark`

Expected: `FAIL directory arrow cleared the screen`. The paste and mark asserts still pass.

- [ ] **Step 3: Draw the chrome once and repaint two rows**

Add the helpers above `show_directory`:

```c
static void draw_dir_row(int index, int selected, int scroll_pos) {
    unsigned char color;
    char line[40];
    int y = index - scroll_pos + 4;
    int x;

    if (index < scroll_pos || index >= scroll_pos + 18 || index >= num_dir_entries)
        return;
    color = (index == selected) ? COL_GREEN : COL_WHITE;
    for (x = 0; x < 40; x++)
        cputc_at(x, y, ' ', color);
    sprintf(line, "%4d  %-16s  %s",
            dir_entries[index].blocks,
            dir_entries[index].name,
            dir_entries[index].type);
    cputs_at(1, y, line, color);
}

static void draw_dir_rows(int selected, int scroll_pos) {
    int i;

    for (i = scroll_pos; i < scroll_pos + 18 && i < num_dir_entries; i++)
        draw_dir_row(i, selected, scroll_pos);
}
```

Move `clrscr`, the title, the disk name, the dash row, the `BLK` header, the file count, and the `UP/DOWN` hint out of the key loop so they run once. Those `cputs_at` strings stay as they are. Then call `draw_dir_rows(selected, scroll_pos)` and enter the loop. The loop is:

```c
    while (1) {
        int old_sel = selected;
        int old_scroll = scroll_pos;
        char c = cgetc();

        if (c == KEY_UP && selected > 0) {
            selected--;
            if (selected < scroll_pos)
                scroll_pos = selected;
        } else if (c == KEY_DOWN && selected < num_dir_entries - 1) {
            selected++;
            if (selected >= scroll_pos + 18)
                scroll_pos = selected - 17;
        } else if (c == KEY_RETURN) {
            /* Keep the existing load body, from show_message("LOADING...")
               through the success return. The open-failure branch prints
               ERROR LOADING and falls through. */
        } else if (c == 3) {
            /* Keep the existing cancel body. */
        }

        if (selected == old_sel && scroll_pos == old_scroll)
            continue;
        if (scroll_pos != old_scroll)
            draw_dir_rows(selected, scroll_pos);
        else {
            draw_dir_row(old_sel, selected, scroll_pos);
            draw_dir_row(selected, selected, scroll_pos);
        }
    }
```

A key that does not move the bar does not draw. A scroll redraws the 18 file rows and leaves the title and the hint. An in-window move fills the old row white and the new row green. Copy the KEY_RETURN and `c == 3` bodies from the current function without editing them.

- [ ] **Step 4: Build, read the map, and run the check**

Run: `cmake --build build --target whisper64`

Then: `grep -E '__bss_end|__heap_start' build/whisper64.map`

If `__bss_end` is past `$CC00`, delete the two-row branch and call `draw_dir_rows` on every selection change, including a one-row move. That still keeps `clrscr` out of the key loop. Rebuild. If that also passes `$CC00`, stop and report the address. Do not shrink an unrelated module to make room.

Run: `python3 tools/c64u.py mark`

Expected: `ok`.

- [ ] **Step 5: Commit**

```bash
git add src/file_ops.c tools/c64u.py
git commit -m "$(cat <<'EOF'
Repaint the two directory rows when the selection moves.

EOF
)"
```

---

### Task 5: Docs, after the Ultimate check is green

**Files:**
- Modify: `docs/Todo-List.md`
- Modify: `AGENTS.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: the passing `python3 tools/c64u.py mark` run from Task 4
- Produces: the two editor bugs checked off, and the mark rules in `AGENTS.md` matching the code

- [ ] **Step 1: Check the todo items off**

Replace the two open editor bullets (the Ctrl+K paragraph and the paste paragraph) with:

```markdown
- [x] Ctrl+K mark movement was calling `update_cursor` on every arrow, and keyword colors were painted after the mark so a purple or blue word stayed that color. Arrow keys now repaint the lines the mark end moved across. A marked keyword is yellow. In 80-column mode a keyword that shares a cell with a marked glyph is yellow too. The directory list was clearing the screen on every up or down. It now redraws the row that lost the selection and the row that gained it. A scroll of that list redraws the 18 file rows. Checked on the Ultimate in 40-column mode with `python3 tools/c64u.py mark`.
- [x] Paste inserted into the buffer and wrote `TEXT PASTED` on the status row, and never redrew the editor lines. The cursor reads the buffer, so the reverse cursor showed the hidden characters. Paste now redraws once, then prints the status line. Checked on the Ultimate with the same `mark` script.
```

- [ ] **Step 2: Update AGENTS.md**

In the screen-modes section, replace the sentences that say a keyword color outranks the mark. The 40-column sentence becomes: a marked column is yellow, including a keyword, and an unmarked statement stays purple and an unmarked function stays blue. The 80-column sentences become: mark yellow outranks keyword purple and blue, which outrank white. A keyword that shares a cell with a marked glyph is yellow. Cell 0 stays cyan.

In the arrow-key paragraph, replace the sentence that says mark still calls `update_cursor`. Mark movement calls `repaint_mark_lines` for the old cursor line and the new one. A phantom-row change, a page change, and a jump of a full screen still call `update_cursor`. Mark arrows leave row 24 alone, except for that phantom-row path.

In the directory sentence, add that F1 paints the title and the hint once. Up and Down repaint the old file row and the new one. A scroll of the list repaints the 18 file rows and does not call `clrscr`.

Where the clipboard is described as two lines, add that `copy_marked` stops at 2. The old loop limit of 8 wrote past the array.

- [ ] **Step 3: Update the README copy line**

Replace the Copy/Paste bullet with:

```markdown
- **Copy/Paste**: Visual mark mode. Copy keeps two lines. A marked keyword turns yellow.
```

- [ ] **Step 4: Commit**

```bash
git add docs/Todo-List.md AGENTS.md README.md
git commit -m "$(cat <<'EOF'
Document mark redraw, yellow keywords, and the directory row update.

EOF
)"
```

80-column mode is the same `cell_rank` and the same `draw_editor_band` path. `tools/c64u.py` cannot read the bitmap. After the 40-column script prints `ok`, say that 80-column was not observed unless Josh watched CTRL+D, a marked `PRINT`, and a paste on the Ultimate or in VICE. Do not write that 80-column was checked if nobody looked at that screen.
