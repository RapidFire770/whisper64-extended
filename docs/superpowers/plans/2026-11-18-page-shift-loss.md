# Page Shift Loss Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deleting a line pulls the next page's first line up, including a join with the previous page, and a Return that pushes lines across full pages finishes in under two seconds without changing the first byte of a moved line.

**Architecture:** `page_shift.c` owns the overlapping byte move and the one-page insert and remove. `delete_char` in `editor.c` joins two lines when they fit in 79 characters, then `pull_hole` walks later pages through the existing `load_page` / `save_current_page_to_temp` path, one carried line at a time, the reverse of `push_spill`. The page move is a `noinline` pointer loop so the link no longer uses the one-byte `memmove` that holds the screen on the old title for about half a second per full page.

**Tech Stack:** C, LLVM-MOS SDK (`c64` platform), host `gcc` for `tools/test_page_insert.c`, `tools/c64u.py` against the C64 Ultimate at `http://192.168.100.193`.

## What was measured

On the current `build/whisper64.prg` (map `__bss_end` `$CAAE`, `lines` at `$BB50`), with `DICEROLL-SRC` loaded and not saved:

- Delete at column 0 of line 2 joins line 1 with `SETUP:` and shifts that page up. The page then has 47 lines. Goto 48 shows `LINE 48 NOT FOUND`. Goto 49 is still the old line 49 (`H6$`). The next page is not pulled up. Delete at column 0 of row 0 does nothing, so a line cannot join the previous page.
- Return at column 0 of line 48 (the last row of a full page) left the screen on `48:1` for 5.54 seconds while RAM walked every later page, then landed on `49:1`. After the walk, page 1 row 0 was the full `H5$` line and row 1 was `H6$`. Page 2 row 1 was `MENUNAV:` and row 2 was `REM READ JOYSTICK INPUT FOR MAIN MENU NAVIGATION`. `IF CS=1 THEN GOSUB RD:GOTO MENU:REM 1=ROLL` was intact. The stored bytes of that cascade did not drop a line and did not replace `I` with a space.
- The shift behind that walk is the LTO `memmove` at `$A5D3`. It copies one byte at a time and rebuilds both end pointers on every byte. A 10-page file costs about 5.5 seconds. The KERNAL keyboard buffer holds 10 keys, and the main loop does not run during the walk, so keys typed while the title is stale apply on the page the cursor lands on.
- `page_insert_line`'s length for a full page is `(47 - row) * 80`, and the short-page length is `(count - row) * 80`. Both are exact in this binary (`$A` is forced to 0 before the multiply). Do not add a special case that patches the first character. Replacing the move, and locking `MENUNAV:` / `IF CS=1` in RAM, is the Return work. The delete pull is the line-loss work.

## Global Constraints

- `__bss_end` stays at or below `$CC00`. `__stack` stays `$D000`. The editor link this plan starts from has `__bss_end` at `$CAAE`, `__heap_start` at `$CACE`, and `lines` at `$BB50` (3840 bytes). Add no BSS. `spill_line` and `spill_next` in `src/editor.c` are the only scratch lines. Do not grow `lines[]`, the undo buffers, or the clipboard.
- `MAX_LINE_LENGTH` is 80 (79 characters plus a NUL). `LINES_PER_PAGE` is 48. A join is allowed only when the two lengths add to less than 80. Do not raise `MAX_LINE_LENGTH`.
- Do not divide or take a remainder by `edit_width`. Do not add `sprintf` in the new editor code.
- `move_bytes` is `noinline`. `shift_rows_40`, `shift_rows_80`, and `shift_viewport` stay `noinline`.
- `num_pages` is the number of stored pages. Do not rebuild it from `total_lines`. A stored count of 0 is not an open page. `push_spill` already skips those REU pages. `pull_hole` skips them the same way.
- A spill clears undo. A pull that touches another page clears undo. A join that stays on the last page keeps the undo `main` saved. A refused join (the two lines do not fit) changes nothing.
- Do not save over `DICEROLL-SRC`. The new hardware script does not write a file. Do not run `tools/test_w64conv_c64u.py`. A VICE run is not the hardware check.
- Build the editor with `cmake --build build --target whisper64` from the repo root. Run the hardware script from the repo root so `build/whisper64.prg` resolves. If the Ultimate does not answer, stop and ask Josh to bring it back online.
- `AGENTS.md` is the current rule. Do not edit `docs/superpowers/specs/2026-11-14-page-spill-design.md`.

## File map

| File | Responsibility |
|---|---|
| `include/page_shift.h`, `src/page_shift.c` | One-page insert, one-page remove, and the overlapping byte move. No KERNAL calls. |
| `tools/test_page_insert.c` | Host checks, including a short page and lines whose first bytes differ. |
| `src/editor.c`, `include/editor.h` | `delete_char` joins and pulls. Return type becomes `int`. |
| `src/main.c` | A line removal redraws the whole screen. |
| `tools/test_page_shift_c64u.py` | 40-column RAM check on the Ultimate. Does not save. |
| `AGENTS.md`, `README.md`, `docs/Todo-List.md` | The pull rule, one README sentence, the checked bug. |

---

### Task 1: Tight page move

**Files:**
- Modify: `include/page_shift.h`
- Modify: `src/page_shift.c`
- Modify: `tools/test_page_insert.c`
- Create: `tools/test_page_shift_c64u.py`
- Test: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c`

**Interfaces:**
- Consumes: `page_insert_line(char page[][MAX_LINE_LENGTH], int *count, int row, const char *text, char *displaced)` — returns 0 when the page grew, 1 when `displaced` received the old last row.
- Produces: `int page_remove_line(char page[][MAX_LINE_LENGTH], int *count, int row, char *removed)`. Returns 0 after copying `page[row]` into `removed`, shifting the later rows up, and decrementing `*count`. Returns 1 when `*count` is 0 or `row` is outside `0 .. *count-1`, and leaves the page alone. `move_bytes` stays static in `src/page_shift.c`.

- [ ] **Step 1: Write the failing test**

Add this function to `tools/test_page_insert.c` and call `test_shift_bytes();` from `main` before `test_pages();`.

```c
static void test_shift_bytes(void) {
    char page[LINES_PER_PAGE][MAX_LINE_LENGTH];
    char displaced[MAX_LINE_LENGTH];
    int count;
    int rc;
    int i;

    memset(page, 0, sizeof page);
    for (i = 0; i < LINES_PER_PAGE; i++) {
        if ((i & 1) == 0)
            strcpy(page[i], "IF CS=1 THEN GOSUB RD");
        else
            strcpy(page[i], " MENUNAV:");
    }
    count = LINES_PER_PAGE;
    rc = page_insert_line(page, &count, 0, "NEW", displaced);
    if (rc != 1 || count != LINES_PER_PAGE || strcmp(page[0], "NEW") != 0)
        fail("full insert head");
    if (strcmp(page[1], "IF CS=1 THEN GOSUB RD") != 0 || page[1][0] != 'I')
        fail("full insert first");
    if (strcmp(page[2], " MENUNAV:") != 0 || page[2][0] != ' ')
        fail("full insert second");
    if (strcmp(displaced, " MENUNAV:") != 0)
        fail("full insert displaced");

    memset(page, 0, sizeof page);
    for (i = 0; i < 47; i++) {
        if ((i & 1) == 0)
            strcpy(page[i], "IF CS=1 THEN GOSUB RD");
        else
            strcpy(page[i], " MENUNAV:");
    }
    count = 47;
    rc = page_insert_line(page, &count, 0, "NEW", displaced);
    if (rc != 0 || count != 48 || strcmp(page[1], "IF CS=1 THEN GOSUB RD") != 0)
        fail("short insert");
    if (strcmp(page[2], " MENUNAV:") != 0 || page[2][0] != ' ')
        fail("short insert first");

    rc = page_remove_line(page, &count, 0, displaced);
    if (rc != 0 || count != 47 || strcmp(displaced, "NEW") != 0)
        fail("remove head");
    if (strcmp(page[0], "IF CS=1 THEN GOSUB RD") != 0 || page[0][0] != 'I')
        fail("remove first");
    if (strcmp(page[1], " MENUNAV:") != 0 || page[1][0] != ' ')
        fail("remove second");

    rc = page_remove_line(page, &count, 47, displaced);
    if (rc != 1 || count != 47)
        fail("remove past end");

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    strcpy(page[2], "CC");
    count = 3;
    rc = page_remove_line(page, &count, 1, displaced);
    if (rc != 0 || count != 2 || strcmp(displaced, "BB") != 0 ||
        strcmp(page[0], "AA") != 0 || strcmp(page[1], "CC") != 0)
        fail("remove middle");
    if (page[2][0] != '\0')
        fail("remove cleared tail");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c`

Expected: the compile or the link names `page_remove_line` and fails.

- [ ] **Step 3: Implement the move**

Replace `include/page_shift.h` with:

```c
#ifndef PAGE_SHIFT_H
#define PAGE_SHIFT_H

#ifndef MAX_LINE_LENGTH
#define MAX_LINE_LENGTH 80
#endif
#ifndef LINES_PER_PAGE
#define LINES_PER_PAGE 48
#endif

int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced);
int page_remove_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, char *removed);

#endif
```

Replace `src/page_shift.c` with:

```c
#include "page_shift.h"
#include <string.h>

static void copy_text(char *dst, const char *text) {
    int i = 0;

    while (text[i] && i < MAX_LINE_LENGTH - 1) {
        dst[i] = text[i];
        i++;
    }
    dst[i] = '\0';
}

__attribute__((noinline))
static void move_bytes(char *dst, const char *src, unsigned int n) {
    if (n == 0 || dst == src)
        return;
    if (dst > src) {
        dst += n;
        src += n;
        while (n != 0) {
            n--;
            dst--;
            src--;
            *dst = *src;
        }
        return;
    }
    while (n != 0) {
        *dst = *src;
        dst++;
        src++;
        n--;
    }
}

int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced) {
    if (*count < LINES_PER_PAGE) {
        move_bytes(page[row + 1], page[row],
                   (unsigned int)(*count - row) * MAX_LINE_LENGTH);
        copy_text(page[row], text);
        (*count)++;
        return 0;
    }

    copy_text(displaced, page[LINES_PER_PAGE - 1]);
    if (row < LINES_PER_PAGE - 1) {
        move_bytes(page[row + 1], page[row],
                   (unsigned int)(LINES_PER_PAGE - 1 - row) * MAX_LINE_LENGTH);
    }
    copy_text(page[row], text);
    return 1;
}

int page_remove_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, char *removed) {
    int n;

    if (*count < 1 || row < 0 || row >= *count)
        return 1;
    copy_text(removed, page[row]);
    n = *count - row - 1;
    if (n > 0) {
        move_bytes(page[row], page[row + 1],
                   (unsigned int)n * MAX_LINE_LENGTH);
    }
    page[*count - 1][0] = '\0';
    (*count)--;
    return 0;
}
```

- [ ] **Step 4: Run the host test to verify it passes**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c && /tmp/test_page_insert`

Expected: exit 0 and no `FAIL` line.

- [ ] **Step 5: Write the hardware timing check**

Create `tools/test_page_shift_c64u.py`. Do not rebuild the editor before the first run. The `whisper64.prg` already on disk is the slow one.

```python
#!/usr/bin/env python3
"""RAM check for a cross-page Return. Does not save."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

K_CTRL_G = 7


def fail(msg):
    print("FAIL", msg)
    drv.dump_rows(label="fail")
    drv.reset()
    sys.exit(1)


def row_bytes(scr, row):
    return bytes(scr[row * 40:(row + 1) * 40])


def lines_addr():
    with open("build/whisper64.map") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) >= 2 and parts[-1] == "lines":
                return int(parts[0], 16)
    fail("lines not in map")


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


def title_field(scr, after_colon):
    raw = bytes(b & 0x7F for b in scr[8:17]).decode("latin1")
    if ":" not in raw:
        return -1
    body = raw.split(":", 1)[1] if after_colon else raw.split(":", 1)[0]
    num = ""
    for ch in body:
        if ch.isdigit():
            num += ch
        elif num:
            break
    return int(num) if num else -1


def title_line(scr):
    return title_field(scr, False)


def ram_row(addr, row):
    blob = drv.read_mem(addr + row * 80, 80)
    return bytes(blob).split(b"\x00", 1)[0]


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if needle in row_bytes(screen(), row):
            return True
        time.sleep(0.3)
    return False


def ensure_w64():
    raw = subprocess.check_output(["c64u", "drives", "list", "--json"], text=True)
    data = json.loads(raw)
    entries = data["drives"] if isinstance(data, dict) and "drives" in data else data
    found = None
    for item in entries:
        for letter, info in item.items():
            if str(info.get("bus_id")) == "8":
                found = (letter, info)
    if found is None:
        fail("no drive with bus_id 8")
    letter, info = found
    image = " ".join(str(info.get(k) or "") for k in ("image_file", "image_path"))
    if "W64.d64" in image:
        return
    if image.strip() == "":
        subprocess.check_call([
            "c64u", "drives", "mount", letter, "/SD/My Files/W64.d64"])
        time.sleep(1)
        return
    fail("drive 8 has a different image: " + image)


def boot():
    drv.reset()
    ensure_w64()
    drv.load_and_run(drv.PRG)
    scr = screen()
    if b"REU" in row_bytes(scr, 24):
        drv.inject_keys([0x20], settle=0.6)


def selected_row(col):
    for r in range(4, 22):
        if (col[r * 40 + 1] & 0x0F) == 5:
            return r
    return -1


def load_named(name):
    drv.inject_keys([drv.K_F1], settle=0.5)
    if not wait_row(0, b"DIRECTORY", 30):
        fail("directory")
    target = name.encode("ascii")
    for _ in range(40):
        scr = screen()
        col = drv.read_mem(drv.COLOR, 1000)
        row = selected_row(col)
        if row >= 0 and target in row_bytes(scr, row):
            drv.inject_keys([drv.K_RETURN], settle=0.5)
            if not wait_row(24, b"LOADED", 90):
                fail("load " + name)
            return
        drv.inject_keys([drv.K_DOWN], settle=0.3)
    fail("find " + name)


def goto_line(n):
    codes = [K_CTRL_G] + [ord(ch) for ch in str(n)] + [drv.K_RETURN]
    drv.inject_keys(codes, settle=0.4)
    deadline = time.time() + 8
    while time.time() < deadline:
        scr = screen()
        if title_line(scr) == n and b"GOTO LINE" not in row_bytes(scr, 24):
            return scr
        time.sleep(0.2)
    fail("goto %d" % n)


def case_return():
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(48)
    started = time.time()
    drv.inject_keys([drv.K_RETURN], settle=0.05)
    landed = None
    while time.time() - started < 2.0:
        if title_line(screen()) == 49:
            landed = time.time() - started
            break
        time.sleep(0.05)
    if landed is None:
        fail("return still on the old line after 2s")
    print("return landed in %.2fs" % landed)
    row0 = ram_row(addr, 0)
    row1 = ram_row(addr, 1)
    if not row0.startswith(b"H5$ = B1$+CHR$(18)"):
        fail("carried H5 %r" % row0[:40])
    if not row1.startswith(b"H6$ = B1$+CHR$(18)"):
        fail("following H6 %r" % row1[:40])
    goto_line(97)
    blob = drv.read_mem(addr, 48 * 80)
    if b"MENUNAV:" not in blob:
        fail("MENUNAV missing")
    if b"RENUNAV" in blob:
        fail("RENUNAV")
    if b"IF CS=1 THEN GOSUB RD:GOTO MENU:REM 1=ROLL" not in blob:
        fail("IF CS=1 missing")
    if b" F CS=1" in blob:
        fail("IF CS=1 lost its I")
    drv.reset()


if __name__ == "__main__":
    case_return()
```

- [ ] **Step 6: Run the hardware check against the current PRG**

Run from the repo root: `python3 -u tools/test_page_shift_c64u.py`

Expected: `FAIL return still on the old line after 2s`. The machine is reset by `fail`. If the Ultimate does not answer, stop and ask Josh.

- [ ] **Step 7: Rebuild and run the hardware check again**

Run: `cmake --build build --target whisper64`

Confirm `build/whisper64.map` contains `move_bytes` and that `__bss_end` is at or below `$cc00`. If `move_bytes` is missing, the function was inlined and the slow loop can come back. Stop and keep it `noinline`. If `__bss_end` is past `$cc00`, stop.

Run: `python3 -u tools/test_page_shift_c64u.py`

Expected: a `return landed in` line under 2 seconds, then a reset to the READY prompt. No `FAIL` line. `MENUNAV:` is present, `RENUNAV` is absent, and `IF CS=1 THEN GOSUB RD:GOTO MENU:REM 1=ROLL` is present.

- [ ] **Step 8: Commit**

```bash
git add include/page_shift.h src/page_shift.c tools/test_page_insert.c tools/test_page_shift_c64u.py
git commit -m "$(cat <<'EOF'
Speed up the page shift and lock the first byte of a moved line.

The generated memmove held the screen on the old title for about half a second per full page, and keys typed in that window landed on the page the cursor jumped to.
EOF
)"
```

---

### Task 2: Pull lines up on delete

**Files:**
- Modify: `include/editor.h` (`delete_char` declaration)
- Modify: `src/editor.c` (`delete_char`, add `pull_hole`)
- Modify: `src/main.c` (the `KEY_DELETE` branch)
- Modify: `tools/test_page_shift_c64u.py`
- Modify: `AGENTS.md`, `README.md`, `docs/Todo-List.md`
- Test: `python3 -u tools/test_page_shift_c64u.py` from the repo root

**Interfaces:**
- Consumes: `page_remove_line` from Task 1. `spill_line`, `spill_next`, `copy_tail`, `load_page`, `save_current_page_to_temp`, `reu_page_lines`, `clear_undo`. `EDIT_HEIGHT` is 23.
- Produces: `int delete_char(void)`. Returns 1 when a line was removed or the cursor changed pages, so `main` calls `update_cursor`. Returns 0 when one character was deleted or the key did nothing, so `main` calls `update_current_line`. `src/clipboard.c` keeps calling `delete_char()` and ignoring the return. Those calls delete characters on the current line (`cursor_x > 0`) and must not pull.

`pull_hole` is static in `src/editor.c`. The page it is called on has `num_lines < 48` and is the hole. It copies the next open page's row 0 onto the end of the hole. When that source page was full, the hole moves there and the walk continues. A source whose REU count is 0 is skipped, matching `push_spill`. When the last page gives up its only line, `num_pages` drops and that empty page is not saved (`page_modified = 0` before the next `load_page`). `total_lines` changes only for the joined line, not for a line that moved between pages.

- [ ] **Step 1: Add the failing hardware cases**

Add these functions to `tools/test_page_shift_c64u.py` and call them from `main` before `case_return()`.

```python
def case_delete_middle():
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(2)
    drv.inject_keys([drv.K_DELETE], settle=0.5)
    goto_line(48)
    row = ram_row(addr, 47)
    if not row.startswith(b"H6$ = B1$+CHR$(18)"):
        fail("page 0 did not pull H6 %r" % row[:40])
    goto_line(49)
    row = ram_row(addr, 0)
    if not row.startswith(b"H7$ = B1$+CHR$(18)"):
        fail("page 1 row 0 %r" % row[:40])
    drv.reset()


def case_delete_page_top():
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(97)
    drv.inject_keys([drv.K_DELETE], settle=0.8)
    scr = screen()
    if title_line(scr) != 96:
        fail("page top join title %s" % title_line(scr))
    row = ram_row(addr, 47)
    if row != b"MENUNAV:":
        fail("joined line %r" % row)
    goto_line(97)
    row = ram_row(addr, 0)
    if not row.startswith(b"REM READ JOYSTICK INPUT"):
        fail("pulled row %r" % row[:40])
    if row[:1] != b"R":
        fail("pulled first byte %r" % row[:4])
    drv.reset()
```

Change the bottom of the script to:

```python
if __name__ == "__main__":
    case_delete_middle()
    case_delete_page_top()
    case_return()
```

Line 96 of `DICEROLL-SRC` is empty and line 97 is `MENUNAV:` (8 characters), so the join fits. Line 1 is 31 characters and line 2 is `SETUP:` (6 characters), so that join fits too. After the middle delete, old line 49 (`H6$`) is title line 48 and old line 50 (`H7$`) is title line 49.

- [ ] **Step 2: Run the hardware check to verify it fails**

Rebuild is already done from Task 1. Run from the repo root: `python3 -u tools/test_page_shift_c64u.py`

Expected: `FAIL goto 48` or `FAIL page 0 did not pull H6`. The later cases do not run. The machine is reset by `fail`.

- [ ] **Step 3: Implement the pull**

In `include/editor.h`, change the declaration to:

```c
int delete_char(void);
```

In `src/editor.c`, add `pull_hole` above `delete_char` and replace `delete_char` with the function below.

```c
static void pull_hole(void) {
    int hole = current_page;
    int source;

    if (num_lines >= LINES_PER_PAGE)
        return;
    if (hole + 1 >= num_pages) {
        if (num_lines == 0 && hole > 0) {
            num_pages = hole;
            page_modified = 0;
            load_page(hole - 1);
        }
        return;
    }

    mark_active = 0;
    clear_undo();
    source = hole + 1;
    for (;;) {
        int was_full;

        if (source >= num_pages)
            break;
        load_page(source);
        if (reu_is_available() && reu_page_lines(source) == 0) {
            source++;
            continue;
        }
        if (num_lines < 1) {
            if (source == num_pages - 1) {
                num_pages = source;
                page_modified = 0;
            }
            break;
        }
        was_full = num_lines == LINES_PER_PAGE;
        page_remove_line(lines, &num_lines, 0, spill_line);
        if (num_lines == 0 && source == num_pages - 1) {
            num_pages = source;
            page_modified = 0;
            was_full = 0;
        } else {
            page_modified = 1;
        }
        load_page(hole);
        copy_tail(lines[num_lines], spill_line);
        num_lines++;
        page_modified = 1;
        if (!was_full)
            break;
        hole = source;
        source = hole + 1;
    }
}

int delete_char(void) {
    int len = strlen(lines[cursor_y]);

    if (cursor_x > 0) {
        memmove(&lines[cursor_y][cursor_x - 1],
                &lines[cursor_y][cursor_x],
                len - cursor_x + 1);
        cursor_x--;
        page_modified = 1;
        return 0;
    }

    if (cursor_y > 0) {
        int prev_len = strlen(lines[cursor_y - 1]);
        int origin;

        if (prev_len + len >= MAX_LINE_LENGTH)
            return 0;
        strcat(lines[cursor_y - 1], lines[cursor_y]);
        page_remove_line(lines, &num_lines, cursor_y, spill_next);
        total_lines--;
        cursor_y--;
        cursor_x = prev_len;
        page_modified = 1;
        if (scroll_offset > 0 && cursor_y < scroll_offset)
            scroll_offset--;
        origin = current_page;
        pull_hole();
        if (current_page != origin && origin < num_pages)
            load_page(origin);
        return 1;
    }

    if (current_page > 0) {
        int origin = current_page;
        int prev_len;
        int joined_page;

        copy_tail(spill_line, lines[0]);
        load_page(origin - 1);
        if (num_lines < 1) {
            load_page(origin);
            return 0;
        }
        prev_len = strlen(lines[num_lines - 1]);
        if (prev_len + len >= MAX_LINE_LENGTH) {
            load_page(origin);
            return 0;
        }
        strcat(lines[num_lines - 1], spill_line);
        page_modified = 1;
        joined_page = current_page;
        save_current_page_to_temp();
        load_page(origin);
        page_remove_line(lines, &num_lines, 0, spill_next);
        total_lines--;
        page_modified = 1;
        pull_hole();
        if (joined_page < num_pages)
            load_page(joined_page);
        cursor_y = num_lines - 1;
        cursor_x = prev_len;
        if (cursor_y >= EDIT_HEIGHT)
            scroll_offset = cursor_y - EDIT_HEIGHT + 1;
        else
            scroll_offset = 0;
        return 1;
    }

    return 0;
}
```

In `src/main.c`, replace the `KEY_DELETE` branch with:

```c
        } else if (c == KEY_DELETE) {
            int old_len = 0;
            int old_x = cursor_x;
            int redraw;

            if (cursor_y < num_lines)
                old_len = (int)strlen(lines[cursor_y]);
            save_undo_state();
            redraw = delete_char();
            if (redraw)
                update_cursor();
            else
                update_current_line(old_len, old_x);
```

`src/clipboard.c` stays as it is. A paste that rolls back calls `delete_char` only to remove characters it just stored, while `cursor_x > 0`.

- [ ] **Step 4: Rebuild and check the map**

Run: `cmake --build build --target whisper64`

`build/whisper64.map` must show `__bss_end` at or below `$cc00`. If it does not, stop. Do not add a buffer to get the pull working.

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c && /tmp/test_page_insert`

Expected: exit 0.

- [ ] **Step 5: Run the hardware check**

Run from the repo root: `python3 -u tools/test_page_shift_c64u.py`

Expected, in order:

- `case_delete_middle` loads `DICEROLL-SRC`, deletes at line 2, and reads `H6$` at page 0 row 47 and `H7$` at page 1 row 0.
- `case_delete_page_top` deletes at line 97, leaves the cursor on title line 96 with RAM row 47 equal to `MENUNAV:`, and title line 97 starting with `REM READ JOYSTICK INPUT` and the byte `R`.
- `case_return` still prints `return landed in` under 2 seconds, with `H5$` / `H6$`, `MENUNAV:`, and the full `IF CS=1 THEN GOSUB RD:GOTO MENU:REM 1=ROLL` line. No `RENUNAV` and no ` F CS=1`.

The script resets after each case. It does not save. If a case fails, fix that case and run the whole script again.

- [ ] **Step 6: Update the docs**

In `AGENTS.md`, in the paragraph that starts with `` `new_line` inserts the tail ``, add this after the sentence that ends with `The old loop limit of 8 wrote past the array.`:

```text
Delete at column 0 joins that line onto the previous line when the two lengths add to less than 80. On row 0 the previous line is the last line of the previous page. A join that does not fit leaves both lines. A join that leaves the page short copies row 0 of the next open page onto the end of this page. A full source page then has the short count, and the copy continues until a page that was not full gives up its row. Only the last page stays short. A stored REU count of 0 is skipped, the same as the spill. Dropping the last page's only line decreases `num_pages` and does not save that empty page. `total_lines` decreases for the joined line and not for a line that only changed pages. A pull clears undo. A join on the last page keeps the undo saved by `main`. `delete_char` returns 1 when the screen has to be redrawn.
```

In that same section, replace:

```text
A join drops `total_lines` and leaves the later pages at the same title numbers, so the check is that page's `num_lines`, not `total_lines`.
```

with:

```text
A join drops `total_lines`. The pull keeps every page except the last one full, so a later title number moves up with its line. The check is that page's `num_lines`, not `total_lines`.
```

In the memory-map pitfall list, add:

```text
- The page shift is `move_bytes` in `src/page_shift.c`, and it is `noinline`. Do not put `memmove` back on the page array. The generated one-byte move held a full-page Return for about half a second per later page, with the title stuck on the old line.
```

In `README.md`, replace the multi-page feature bullet with:

```markdown
- **Multi-Page Editing**: Pages stored in REU or disk temp files. Return on a full page pushes the last line onto the next page. Delete at the start of a line joins it with the line above, including the last line of the previous page, and pulls the next page's first line up so only the last page stays short.
```

In `docs/Todo-List.md`, change the page-shift bug from `- [ ]` to `- [x]`. Leave the sentence as it is.

- [ ] **Step 7: Commit**

```bash
git add include/editor.h src/editor.c src/main.c tools/test_page_shift_c64u.py AGENTS.md README.md docs/Todo-List.md
git commit -m "$(cat <<'EOF'
Pull the next page up when a line is deleted.

A join used to leave a short page in the middle of the file, so the following lines kept their old title numbers and a later goto reported the missing row.
EOF
)"
```
