# Wrapped-line refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Typing on a wrapped line updates that line's screen rows, and repaints the rows below only when the line's screen-row count changes.

**Architecture:** `update_current_line` already paints one screen row when every visible editor line is one row. A wrap, or a wrap above the cursor, currently falls through to `update_cursor` → `redraw_screen`, which blanks the title, walks every visible line, and clears the status row. The same function instead walks to the cursor line with the existing layout helpers and calls `draw_editor_band` for that line's rows. The tail from that line to row 23 is drawn only when `display_rows_for` changes. A scroll still uses `update_cursor`. No new buffer and no new global.

**Tech Stack:** C, LLVM-MOS SDK (`c64` platform), host `gcc` for `tools/test_cursor_layout.c`, `tools/c64u.py` against the C64 Ultimate at `http://192.168.100.193`.

## Global Constraints

- Editor link: `__bss_end` must stay at or below `$CC00`. `__stack` stays `$D000`. The October 24, 2026 link has `__bss_end` at `$CB18`, `__heap_start` at `$CB38`, and `__stack` at `$D000`. Stop if a rebuild puts `__bss_end` past `$CC00`.
- `shift_rows_40`, `shift_rows_80`, and `shift_viewport` in `src/screen.c` stay `noinline`. Inlining them put `__bss_end` at `$CC32`.
- Do not divide or take a remainder by `edit_width`. It is 37 or 77. Compare twice, the way `text_rows_for` does. A subtract loop is rewritten into a divide.
- Do not grow `lines[]`, the undo buffers, or the clipboard. Do not add a RAM file buffer. Do not add a `lens[]` array on the typing path.
- One editor line is at most 79 characters. A line is at most three screen rows. Return is the only new editor line. Do not split the buffer at `edit_width`.
- Do not call the KERNAL between `screen80_begin_draw` and `screen80_end_draw`. `draw_title_pos` already runs inside that window on the fast path. Do not add another `sprintf` there.
- The status row is row 24. `redraw_screen` clears it. The typing path must not.
- 80-column draws of text go through `draw_slice` / `render_line_80` while the bitmap is banked in. `tools/c64u.py` reads `$0400` only. It cannot see 80-column bitmap mode.
- Do not claim a C64 behavior works unless VICE or the Ultimate showed it. If the Ultimate at `http://192.168.100.193` is unreachable, pause and ask Josh to bring it back online.
- Host layout check: `gcc -Wall -Wextra -Iinclude -o /tmp/test_cursor_layout tools/test_cursor_layout.c && /tmp/test_cursor_layout`. Expected output is `ok`.
- Build from `build/` with the existing CMake cache. Output includes `build/whisper64.prg`. Do not compile the three tools at `-Os`.

---

## Root cause

`src/main.c` calls `update_current_line` after a stored character. `src/screen.c` `line_is_simple` returns 0 when the current line's length or cursor column is at least `edit_width`, and when any editor line from `scroll_offset` to the cursor has length greater than `edit_width`. `update_current_line` then calls `update_cursor`, which calls `redraw_screen`.

That full redraw is required only when this keystroke changes how many screen rows the line occupies, or when `fit_cursor` changes `scroll_offset`. In 40-column mode those row-count changes are the keystroke that lands on column 37 and the one that lands on column 74. Every other character on that line keeps the same row count. A short line typed under a wrapped line also keeps its row count. Both cases still take the full redraw today, and the one-row formula `cursor_y - scroll_offset + 1` would paint the wrong row while a wrap sits above the cursor.

`display_rows_for` is `text_rows_for` plus `phantom_row_for`. A cursor sitting on a row boundary (`col == 0` past the end of the text) adds the blank continuation row. One stored character changes that total by at most 1. Delete at column 0 joins lines and still uses `update_cursor`. Return still uses `update_cursor`.

## File map

| File | Responsibility |
|---|---|
| `include/cursor_layout.h` | `display_rows_for`, shared by the editor and the host test |
| `tools/test_cursor_layout.c` | Host checks for the row count around a wrap boundary |
| `include/screen.h` | `update_current_line(int old_len, int old_x)` |
| `src/screen.c` | Band redraw inside `update_current_line` |
| `src/main.c` | Pass the length and column from before the insert or delete |
| `tools/c64u.py` | 40-column Ultimate check that wrapped typing leaves the status row and the rows outside the line |
| `docs/Todo-List.md` | Check off the wrap-refresh bug after the Ultimate check |
| `AGENTS.md` | Replace the sentence that says a wrap uses the full redraw |

---

### Task 1: Row-count helper

**Files:**
- Modify: `include/cursor_layout.h`
- Modify: `tools/test_cursor_layout.c`
- Test: `tools/test_cursor_layout.c`

**Interfaces:**
- Consumes: `text_rows_for`, `phantom_row_for` in `include/cursor_layout.h`
- Produces: `static inline int display_rows_for(int len, int width, int on_cursor, int cx)` returning `text_rows_for(len, width) + phantom_row_for(len, width, on_cursor, cx)`

- [ ] **Step 1: Write the failing test**

Add these expects to `main` in `tools/test_cursor_layout.c`, after the existing `phantom 77` expect and before the `lens[0] = 37` block:

```c
    expect_int("rows 36", display_rows_for(36, 37, 1, 36), 1);
    expect_int("rows 37", display_rows_for(37, 37, 1, 37), 2);
    expect_int("rows 38", display_rows_for(38, 37, 1, 38), 2);
    expect_int("rows 73", display_rows_for(73, 37, 1, 73), 2);
    expect_int("rows 74", display_rows_for(74, 37, 1, 74), 3);
    expect_int("rows 75", display_rows_for(75, 37, 1, 75), 3);
    expect_int("rows mid", display_rows_for(50, 37, 1, 10), 2);
    expect_int("rows 77", display_rows_for(77, 77, 1, 77), 2);
    expect_int("rows 78", display_rows_for(78, 77, 1, 78), 2);
    expect_int("rows empty", display_rows_for(0, 37, 1, 0), 1);
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_cursor_layout tools/test_cursor_layout.c && /tmp/test_cursor_layout`

Expected: compile error, `display_rows_for` undeclared.

- [ ] **Step 3: Add the helper**

In `include/cursor_layout.h`, immediately after `phantom_row_for` and before `scroll_rows`:

```c
static inline int display_rows_for(int len, int width, int on_cursor, int cx) {
    return text_rows_for(len, width) + phantom_row_for(len, width, on_cursor, cx);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_cursor_layout tools/test_cursor_layout.c && /tmp/test_cursor_layout`

Expected: `ok`

- [ ] **Step 5: Commit**

```bash
git add include/cursor_layout.h tools/test_cursor_layout.c
git commit -m "$(cat <<'EOF'
Add the screen-row count used by wrapped typing.

EOF
)"
```

---

### Task 2: Redraw the wrapped line, not the window

**Files:**
- Modify: `include/screen.h` (the `update_current_line` declaration)
- Modify: `src/screen.c` (`update_current_line`, about lines 677–704)
- Modify: `src/main.c` (the `KEY_DELETE` branch about lines 131–145, and the printable-character branch about lines 255–266)

**Interfaces:**
- Consumes: `display_rows_for(int len, int width, int on_cursor, int cx)` from Task 1. `draw_editor_band(int first, int last)`, `cursor_screen`, `fit_cursor`, `erase_cursor`, `draw_title_pos`, `draw_cursor`, `line_is_simple`, `text_rows`, `split_col` already in `src/screen.c`. `edit_width`, `cursor_x`, `cursor_y`, `scroll_offset`, `num_lines`, `lines`, `screen_mode`, `EDIT_HEIGHT`.
- Produces: `void update_current_line(int old_len, int old_x)`. `old_len` and `old_x` are the line length and cursor column before `insert_char` or `delete_char`. Callers that did not change the line do not call it.

- [ ] **Step 1: Change the declaration**

In `include/screen.h`, replace

```c
void update_current_line(void);  // fast: redraws only current line + cursor
```

with

```c
void update_current_line(int old_len, int old_x);
```

- [ ] **Step 2: Replace `update_current_line`**

Replace the function body in `src/screen.c`. Keep `draw_editor_band` and `line_is_simple` as they are. Do not add a length array. Do not use `/` or `%` on `edit_width`.

```c
void update_current_line(int old_len, int old_x) {
    int new_len = 0;
    int on_line;
    int new_rows;
    int old_rows;
    int old_scroll;
    int sx, sy, part, col, first, last;

    if (cursor_y < num_lines)
        new_len = (int)strlen(lines[cursor_y]);
    on_line = cursor_y < num_lines;
    new_rows = display_rows_for(new_len, edit_width, on_line, cursor_x);
    old_rows = display_rows_for(old_len, edit_width, on_line, old_x);

    if (old_rows == 1 && new_rows == 1 && line_is_simple()) {
        int screen_y = cursor_y - scroll_offset + 1;

        if (screen_mode == MODE_80COL)
            screen80_begin_draw();
        draw_title_pos();
        if (screen_y >= 1 && screen_y <= EDIT_HEIGHT) {
            if (screen_mode != MODE_80COL)
                draw_line_number(screen_y, cursor_y);
            draw_text_line(screen_y, cursor_y);
        }
        draw_cursor();
        if (screen_mode == MODE_80COL)
            screen80_end_draw();
        return;
    }

    old_scroll = scroll_offset;
    fit_cursor();
    if (scroll_offset != old_scroll) {
        update_cursor();
        return;
    }

    cursor_screen(&sx, &sy);
    split_col(cursor_x, &part, &col);
    first = sy - part;
    if (first < 1)
        first = 1;
    if (new_rows != old_rows)
        last = EDIT_HEIGHT;
    else {
        last = first + new_rows - 1;
        if (last > EDIT_HEIGHT)
            last = EDIT_HEIGHT;
    }

    if (screen_mode == MODE_80COL)
        screen80_begin_draw();
    erase_cursor();
    draw_title_pos();
    draw_editor_band(first, last);
    draw_cursor();
    if (screen_mode == MODE_80COL)
        screen80_end_draw();
}
```

`cursor_screen` counts `text_rows` of each earlier line and then adds the cursor's wrap part. Subtracting that part is the first screen row of this editor line. Earlier lines have no phantom row. `draw_editor_band` uses the same row count, including the phantom row on the cursor line.

When `new_rows == old_rows`, `last` covers only this line. Rows under it stay. When the count changes, `last` is `EDIT_HEIGHT` (23), so the lines under it move down or up. Row 24 is not in that range.

`fit_cursor` runs on every band update. A cursor that steps onto a continuation row past row 23 scrolls even when the line's row count stays the same. If `scroll_offset` moves, this keystroke uses `update_cursor`. That is one full redraw when the cursor is already at the bottom of the window. The walk is line lengths only. The rows below are still drawn only when `new_rows != old_rows`.

- [ ] **Step 3: Pass the old length from `main.c`**

Replace the `KEY_DELETE` branch with:

```c
        } else if (c == KEY_DELETE) {
            int at_break = (cursor_x == 0 && cursor_y > 0);
            int old_len = 0;
            int old_x = cursor_x;

            if (!at_break && cursor_y < num_lines)
                old_len = (int)strlen(lines[cursor_y]);
            save_undo_state();
            delete_char();
            if (at_break)
                update_cursor();
            else
                update_current_line(old_len, old_x);
        }
```

Delete at column 0 joins two editor lines. That still uses `update_cursor`. A delete that only shortens the current line uses the band path, including when the line is wrapped.

Replace the printable-character branch with:

```c
        else if (c >= 32 && c < 128) {
            int old_x = cursor_x;
            int old_len = 0;

            if (cursor_y < num_lines)
                old_len = (int)strlen(lines[cursor_y]);
            if (old_len < MAX_LINE_LENGTH - 1)
                save_undo_state();
            insert_char(c);
            if (cursor_x != old_x)
                update_current_line(old_len, old_x);
        }
```

A rejected 80th character leaves `cursor_x` unchanged, so `BASIC LINE FULL` stays on the status row.

- [ ] **Step 4: Build**

Run: `cmake --build build`

Expected: `build/whisper64.prg` links. No new warnings from `screen.c` or `main.c`.

- [ ] **Step 5: Commit**

```bash
git add include/screen.h src/screen.c src/main.c
git commit -m "$(cat <<'EOF'
Redraw a wrapped line instead of the whole window.

EOF
)"
```

---

### Task 3: Map gate

**Files:**
- Modify, only if the gate fails: `src/screen.c` (`draw_editor_band`)
- Read: `build/whisper64.map`

**Interfaces:**
- Consumes: the Task 2 link
- Produces: a link whose `__bss_end` is at or below `$CC00` and whose `__stack` is `$D000`

- [ ] **Step 1: Read the three symbols**

Run:

```bash
python3 - <<'PY'
import re
text = open("build/whisper64.map").read()
for name in ("__bss_end", "__heap_start", "__stack"):
    m = re.search(r"(?:0x)?([0-9A-Fa-f]{4,})\s+" + name + r"\b", text)
    if not m:
        m = re.search(name + r"\s*=\s*(?:0x)?([0-9A-Fa-f]+)", text)
    print(name, m.group(1) if m else "MISSING")
PY
```

Expected: all three found, `__stack` is `d000` (any hex case), `__bss_end` is less than or equal to `cc00`. `__heap_start` stays below `__stack`. Record the three addresses in the commit message body of the docs task.

- [ ] **Step 2: If `__bss_end` is above `$CC00`, stop the inlining**

Add the same attribute the shift helpers use, on `draw_editor_band` only:

```c
__attribute__((noinline)) static void draw_editor_band(int first, int last) {
```

Rebuild with `cmake --build build` and run the Python reader again.

If `__bss_end` is still above `$CC00`, stop. Do not add a buffer, do not inline `shift_rows_40`, `shift_rows_80`, or `shift_viewport`, and do not ship the PRG. Report the two map readings.

- [ ] **Step 3: Commit, only when Step 2 changed the file**

```bash
git add src/screen.c
git commit -m "$(cat <<'EOF'
Keep the editor band draw from moving bss past $CC00.

EOF
)"
```

Skip this commit when Step 1 already passed.

---

### Task 4: Ultimate check and the docs

**Files:**
- Modify: `tools/c64u.py`
- Modify: `docs/Todo-List.md` (the unchecked wrap-refresh bullet)
- Modify: `AGENTS.md` (the `update_current_line` sentence in "How the editor stores text" / the screen-update paragraph)

**Interfaces:**
- Consumes: `boot_editor`, `inject_keys`, `dump_rows`, `row_text`, `read_mem`, `SCREEN` in `tools/c64u.py`. Delete is PETSCII `0x14` (`KEY_DELETE` in `include/whisper64.h`).
- Produces: `python3 tools/c64u.py wrap` prints `ok` on a reachable Ultimate. The todo bullet is checked. `AGENTS.md` describes the band redraw.

- [ ] **Step 1: Add `cmd_wrap`**

`tools/c64u.py` cannot see 80-column mode. This command checks 40-column mode only. Add `K_DELETE = 0x14` next to the other key constants. Add `cmd_wrap`, and dispatch `wrap` from `main` the same way as `cursor`.

The sequence, after `boot_editor()`:

Return calls `update_cursor`, and `redraw_screen` clears row 24. F4 calls `show_message` after that full redraw, which is what puts `BASIC` on row 24. F4 therefore comes after the line is set up.

1. `inject_keys([K_RETURN], settle=0.4)`, then `inject_keys(list(b"HI"), settle=0.4)`, then `inject_keys([K_UP], settle=0.4)`. Line 0 is empty and the cursor is on it. Line 1 is `HI`.
2. `inject_keys([0x58] * 36, settle=0.8)`. Line 0 is 36 `X`s, still one screen row.
3. `inject_keys([K_F4], settle=0.6)`. Status row 24 contains `BASIC`. Row 1 starts `00:` (`0x30, 0x30, 0x3A`) followed by 36 `0x58`. Row 2 starts `01:` (`0x30, 0x31, 0x3A`) then `HI` (`0x48, 0x49`). `redraw_screen` clears row 24, so a later read that lacks `BASIC` means a full redraw ran.
4. `inject_keys([0x58], settle=0.5)`. This is the phantom row. Row 1 is `00:` and 37 `0x58`. Row 2 starts with three spaces. Row 3 starts `01:` then `HI`. Row 24 still contains `BASIC`. Save this screen image.
5. `inject_keys([0x58] * 3, settle=0.6)`. Length is 40. Three `X`s sit on screen row 2. Row count stays 2. Compare with the saved image:
   - Row 24 is unchanged.
   - Row 3 is unchanged (`01:` and `HI`).
   - Rows 4 through 23 are unchanged.
   - Row 0 changes only in columns 8 through 16 (the `line:col` field `draw_title_pos` writes).
   - Row 2 column 3, masked with `0x7F`, is `0x58`.
   Print `FAIL` and return 1 on any miss.
6. `inject_keys([K_DELETE] * 3, settle=0.5)`. Length 37 is the phantom row, still 2 screen rows. Row 3 and row 24 match the image from step 5. Row 2 column 3, masked with `0x7F`, is `0x20`.
7. `inject_keys([K_DELETE], settle=0.5)`. Length 36 is one screen row again. Row 2 starts `01:` then `HI`. Row 24 still contains `BASIC`.

Mask the cursor cell with `0x7F` when comparing text bytes. The cursor is screen code `0xA0` past the last character. Compare color RAM only for the status-row check, which is the screen bytes containing `BASIC`.

Print `ok` and return 0 when every check holds.

- [ ] **Step 2: Run it on the Ultimate**

Run: `python3 tools/c64u.py wrap`

Expected: `ok`. This resets the Ultimate and runs `build/whisper64.prg`. If the host does not answer, stop and ask Josh. Do not substitute VICE for this script.

80-column mode uses the same `update_current_line`. The script cannot read the bitmap. Do not report 80-column speed from this run. Josh can press CTRL+D and type past column 77 to see that mode.

- [ ] **Step 3: Update the todo and `AGENTS.md`**

In `docs/Todo-List.md`, check the bullet that starts `When typing in 40 or 80 column mode the screen fully refreshes`. Append one sentence: a wrapped keystroke redraws that line's screen rows, and the rows below move only when the line's screen-row count changes. The Ultimate `wrap` check saw `BASIC` stay on row 24 while three more characters were typed on a 37-character line, and `HI` stayed on its row.

In `AGENTS.md`, replace both of these sentences:

- "A rejected character must not call `update_current_line`: a 79-character line is wrapped, and that redraw clears the status row." The skip stays, because the character was not stored. The band redraw does not clear row 24.
- "A wrap uses the full redraw."

The replacement for the second sentence: `update_current_line` takes the length and column from before the keystroke. One screen row and no wrap above the cursor still redraws that row. A wrap redraws the screen rows of that editor line. The rows below are redrawn when `display_rows_for` changes. A scroll still calls `update_cursor`. The status row is left alone. Record the new `__bss_end`, `__heap_start`, and `__stack` from Task 3 next to the existing map sentence if any of the three moved.

- [ ] **Step 4: Commit**

```bash
git add tools/c64u.py docs/Todo-List.md AGENTS.md
git commit -m "$(cat <<'EOF'
Record the wrapped-line refresh and check it on the Ultimate.

EOF
)"
```

Include `src/screen.c` in this commit only if Task 3's noinline edit is still uncommitted.

---

## Spec coverage

- Typing in 40-column mode on a wrapped line does not call `redraw_screen`: Task 2, proved by the status row in Task 4.
- The same code runs in 80-column mode: Task 2. Bitmap contents are not machine-checked.
- A line that is not wrapped stays on the one-row path: the `old_rows == 1 && new_rows == 1 && line_is_simple()` branch in Task 2.
- A wrap above the cursor paints the real screen row: `cursor_screen` minus the cursor part, Task 2. Task 4 step 5 types on the wrapped line itself. The short line under it is the tail that must stay put.
- Size: Task 3. No new global, no `lens[]` on the typing path.
- Delete of one character on a wrapped line uses the same band: Task 2 step 3 and Task 4 step 6. Delete at column 0 still joins lines with `update_cursor`.

## Out of scope

- Arrow keys stay on `refresh_move`. Moving onto a phantom row can still call `update_cursor` once.
- Return, paste, page changes, and goto still call `update_cursor`.
- No change to `W64CONV`, `W64RENUM`, or `W64LABEL`.
