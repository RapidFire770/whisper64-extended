# Save Check and Mark Delete Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the `-Oz` editor still loads and saves `DICEROLL-SRC` without changing a byte, then make DEL in mark mode delete the selected lines.

**Architecture:** Task 1 is a hardware round trip only. It does not change the editor. A failure stops the plan: no mark-delete, no merge. Task 2 adds `delete_marked_lines` in `src/editor.c` because `pull_hole` is static there. DEL with a mark removes every whole line from the top of the mark through the bottom, then pulls the next pages up once per removed line. DEL with no mark stays the existing character and join behavior.

**Tech Stack:** C, LLVM-MOS SDK (`c64` platform), host `gcc` for `tools/test_page_insert.c`, `tools/c64u.py` against the C64 Ultimate at `http://192.168.100.193`.

## Global Constraints

- Work on branch `page-shift-loss` in `/home/josh/Documents/Programming/C64/whisper64-extended/.worktrees/page-shift-loss`. Do not edit the main checkout. Do not amend commits already on the branch.
- The ten `-Oz` files stay `-Oz`. Do not move `screen.c`, `main.c`, `file_ops.c`, `help.c`, `search.c`, `clipboard.c`, `undo.c`, `goto.c`, `screen80.c`, or `reu.c` back to `-Os` in this plan. `editor.c` and `page_shift.c` stay `-Os`.
- `__bss_end` stays at or below `$CC00`. `__stack` stays `$D000`. Add no BSS. Do not grow `lines[]`, the undo buffers, or the clipboard.
- `MAX_LINE_LENGTH` is 80. `LINES_PER_PAGE` is 48. Do not divide or take a remainder by `edit_width`.
- Do not save over `DICEROLL-SRC`. The save name is `OPTCHK`. The prompt keeps the old name inside the brackets. The name that will be written is the editable field, which starts at column `9 + length + 3`. If that field still holds a name after the deletes, or does not show `OPTCHK` before Return, reset the machine and fail. Do not press Return on an empty name: an empty name saves as the current filename.
- The host bytes are `/home/josh/Documents/Programming/C64/dice-roll/src/diceroll-src.seq`, split on CR. 444 lines. Do not modify that file.
- Do not run `tools/test_w64conv_c64u.py`. A VICE run is not the hardware check. If the Ultimate does not answer, stop.
- Build with `cmake --build build --target whisper64.prg` from the worktree. Run hardware scripts from the worktree root. `lines` is read from `build/whisper64.map`, not a hardcoded address.
- Task 1 must pass on the current editor before Task 2 starts. A Task 1 failure is `BLOCKED`. Do not patch `file_ops.c` to force the test green.

## File map

| File | Responsibility |
|---|---|
| `tools/test_save_roundtrip_c64u.py` | Load, save as `OPTCHK`, reload, and compare every line with the host SEQ. Then one edit on page 0 and one on a later page, save, reload, compare again. |
| `src/editor.c`, `include/editor.h`, `src/main.c` | DEL with a mark deletes those lines and pulls. |
| `tools/test_mark_delete_c64u.py` | Character delete still deletes one character. A three-line mark deletes those lines and the following file bytes stay in order. |
| `AGENTS.md`, `README.md`, `docs/Todo-List.md` | The mark-delete sentence and the checked todo. |

---

### Task 1: Save and load round trip

**Files:**
- Create: `tools/test_save_roundtrip_c64u.py`
- Test: `python3 -u tools/test_save_roundtrip_c64u.py` from the worktree root

**Interfaces:**
- Consumes: the editor already built at `build/whisper64.prg`. F2 is PETSCII 137. Delete is `0x14`. The save prompt is row 24. `SAVED!` is the success text. `lines` in the map is the 48 by 80 buffer.
- Produces: a script that exits 0 only when both round trips match the host bytes. No editor change.

- [ ] **Step 1: Write the script**

Create `tools/test_save_roundtrip_c64u.py`:

```python
#!/usr/bin/env python3
"""Load and save DICEROLL-SRC through the -Oz editor. Does not replace that file."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

HOST = "/home/josh/Documents/Programming/C64/dice-roll/src/diceroll-src.seq"
K_F2 = 137
K_CTRL_G = 7
SAVE_NAME = b"OPTCHK"


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


def host_lines():
    data = open(HOST, "rb").read().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    lines = data.split(b"\n")
    if lines and lines[-1] == b"":
        lines.pop()
    return lines


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


def title_line(scr):
    raw = bytes(b & 0x7F for b in scr[8:17]).decode("latin1")
    if ":" not in raw:
        return -1
    num = ""
    for ch in raw.split(":", 1)[0]:
        if ch.isdigit():
            num += ch
        elif num:
            break
    return int(num) if num else -1


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if needle in row_bytes(screen(), row):
            return True
        time.sleep(0.3)
    return False


def wait_status(needles, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        row = row_bytes(screen(), 24)
        for needle in needles:
            if needle in row:
                return needle
        time.sleep(0.3)
    return None


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
    if b"REU" in row_bytes(screen(), 24):
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
            return
        time.sleep(0.2)
    fail("goto %d" % n)


def read_all(addr, n):
    got = []
    page = 0
    while len(got) < n:
        goto_line(page * 48 + 1)
        blob = bytes(drv.read_mem(addr, 48 * 80))
        take = min(48, n - len(got))
        for row in range(take):
            raw = blob[row * 80:(row + 1) * 80].split(b"\x00", 1)[0]
            got.append(raw)
        page += 1
        if page > 20:
            fail("too many pages")
    return got


def compare(label, got, want):
    if len(got) != len(want):
        fail("%s count %d want %d" % (label, len(got), len(want)))
    for i, (g, w) in enumerate(zip(got, want), 1):
        if g != w:
            fail("%s line %d got %r want %r" % (label, i, g[:60], w[:60]))
    print(label, "ok", len(got), "lines")


def save_as(name, current_len):
    # The bracketed label keeps the old name. The editable copy starts
    # at column 9 + len + 3. Only that field decides the saved name.
    field = 9 + current_len + 3
    drv.inject_keys([K_F2], settle=0.5)
    if not wait_row(24, b"SAVE AS", 10):
        fail("save prompt")
    drv.inject_keys([drv.K_DELETE] * 16, settle=0.45)
    row = row_bytes(screen(), 24)
    if row[field:field + current_len].strip(b" \x00"):
        fail("name still in the edit field %r" % row[field:field + 16])
    drv.inject_keys(list(name), settle=0.4)
    row = row_bytes(screen(), 24)
    if row[field:field + len(name)] != name:
        fail("typed name missing %r" % row[field:field + 16])
    if b"DICEROLL" in row[field:]:
        fail("edit field still has DICEROLL")
    drv.inject_keys([drv.K_RETURN], settle=0.4)
    hit = wait_status([b"OVERWRITE", b"SAVED", b"SAVE ERROR", b"CANCELLED"], 180)
    if hit == b"OVERWRITE":
        drv.inject_keys([ord("Y")], settle=0.4)
        hit = wait_status([b"SAVED", b"SAVE ERROR"], 180)
    if hit != b"SAVED":
        fail("save result %s" % hit)


def main():
    want = host_lines()
    if len(want) != 444:
        fail("host line count %d" % len(want))
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    compare("load", read_all(addr, 444), want)
    save_as(SAVE_NAME, 12)
    load_named("OPTCHK")
    compare("reload", read_all(addr, 444), want)
    goto_line(2)
    drv.inject_keys([ord("X")], settle=0.45)
    goto_line(97)
    drv.inject_keys([ord("Q")], settle=0.45)
    edited = list(want)
    edited[1] = b"X" + edited[1]
    edited[96] = b"Q" + edited[96]
    save_as(SAVE_NAME, 6)
    load_named("OPTCHK")
    compare("edited reload", read_all(addr, 444), edited)
    drv.reset()
    print("save round trip passed")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

The editor binary is already `build/whisper64.prg`. Do not rebuild before this run. From the worktree root, with network access to `192.168.100.193`:

Run: `python3 -u tools/test_save_roundtrip_c64u.py`

Expected: `load ok 444 lines`, `reload ok 444 lines`, `edited reload ok 444 lines`, `save round trip passed`, exit 0. The machine is left at the READY prompt.

A `FAIL` line stops this plan. Report the line number, the got bytes, and the wanted bytes. Do not edit `src/file_ops.c`.

- [ ] **Step 3: Commit the script**

```bash
git add tools/test_save_roundtrip_c64u.py
git commit -m "$(cat <<'EOF'
Check that load and save round-trip the dice SEQ.

The -Oz editor has to put every DICEROLL-SRC byte back, including an edit on a later page, and it must not write that file under its original name.
EOF
)"
```

---

### Task 2: Delete marked lines

**Files:**
- Modify: `include/editor.h`
- Modify: `src/editor.c`
- Modify: `src/main.c` (the `KEY_DELETE` branch)
- Create: `tools/test_mark_delete_c64u.py`
- Modify: `AGENTS.md`, `README.md`, `docs/Todo-List.md`
- Test: `python3 -u tools/test_mark_delete_c64u.py` and `python3 -u tools/test_save_roundtrip_c64u.py`

**Interfaces:**
- Consumes: `page_remove_line`, static `pull_hole`, `clear_undo`, `mark_start_y`, `mark_end_y`, `mark_active`. CTRL+K is PETSCII 11 (`drv.K_CTRL_K`). Down is `drv.K_DOWN`.
- Produces: `int delete_marked_lines(void)`. Returns 1 after the lines are removed. Returns 0 when the mark does not cover a stored line.

The mark may start below the cursor. Use the lower row and the higher row. Delete each of those editor lines whole, including when the mark only covers part of the first or last line. Do not join the text onto the line above. Remove from the bottom row upward so the indexes stay put. `total_lines` drops by that count. Then call `pull_hole` once per removed line. Restore the page the mark was on when it still exists. Cursor column 0 on the row where the block started, clamped to the lines that remain. `mark_active = 0`. `clear_undo` always, because the one-line undo cannot restore a block. `page_modified = 1`.

`main` calls this only when `mark_active` is set. Otherwise DEL stays `delete_char`.

- [ ] **Step 1: Write the failing hardware test**

Create `tools/test_mark_delete_c64u.py` by copying the helpers `fail`, `row_bytes`, `lines_addr`, `host_lines`, `screen`, `title_line`, `wait_row`, `ensure_w64`, `boot`, `selected_row`, `load_named`, `goto_line`, `read_all`, and `compare` from `tools/test_save_roundtrip_c64u.py`. Do not copy `save_as`. The script's `main` is:

```python
def main():
    want = host_lines()
    addr = lines_addr()
    boot()
    load_named("DICEROLL-SRC")
    goto_line(1)
    drv.inject_keys([drv.K_RIGHT, drv.K_DELETE], settle=0.5)
    row = bytes(drv.read_mem(addr, 80)).split(b"\x00", 1)[0]
    if row != b"RM === MAIN INITIALIZATION ===":
        fail("char delete %r" % row[:40])
    boot()
    load_named("DICEROLL-SRC")
    goto_line(2)
    drv.inject_keys([drv.K_CTRL_K, drv.K_DOWN, drv.K_DOWN, drv.K_DELETE], settle=0.6)
    deadline = time.time() + 8
    expect = want[:1] + want[4:]
    got = None
    while time.time() < deadline:
        goto_line(2)
        row = bytes(drv.read_mem(addr + 80, 80)).split(b"\x00", 1)[0]
        if row == expect[1]:
            got = read_all(addr, len(expect))
            break
        time.sleep(0.3)
    if got is None:
        fail("marked lines still present")
    compare("mark delete", got, expect)
    drv.reset()
    print("mark delete passed")
```

`expect[1]` is host line 5 (`REM BORDER CHARACTERS`), which slides into editor line 2 after lines 2, 3, and 4 are removed. `read_all` still walks `page * 48 + 1`, so a short page must not be read past `len(expect)`. The helper from Task 1 already stops at `n`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -u tools/test_mark_delete_c64u.py`

Expected: `FAIL char delete` does not happen, and the mark half fails with `marked lines still present` or a compare failure. The current DEL joins or deletes a character instead of removing the three lines. The machine is reset by `fail`.

- [ ] **Step 3: Implement the delete**

In `include/editor.h`, next to `delete_char`:

```c
int delete_marked_lines(void);
```

In `src/editor.c`, after `pull_hole`:

```c
int delete_marked_lines(void) {
    int y0 = mark_start_y < mark_end_y ? mark_start_y : mark_end_y;
    int y1 = mark_start_y < mark_end_y ? mark_end_y : mark_start_y;
    int n;
    int i;
    int origin;

    if (y0 < 0)
        y0 = 0;
    if (y1 >= num_lines)
        y1 = num_lines - 1;
    if (y0 > y1)
        return 0;
    n = y1 - y0 + 1;
    for (i = y1; i >= y0; i--)
        page_remove_line(lines, &num_lines, i, spill_next);
    total_lines -= n;
    page_modified = 1;
    mark_active = 0;
    cursor_x = 0;
    cursor_y = y0;
    if (num_lines < 1)
        cursor_y = 0;
    else if (cursor_y >= num_lines)
        cursor_y = num_lines - 1;
    origin = current_page;
    for (i = 0; i < n; i++)
        pull_hole();
    if (current_page != origin && origin < num_pages)
        load_page(origin);
    if (num_lines < 1)
        cursor_y = 0;
    else if (cursor_y >= num_lines)
        cursor_y = num_lines - 1;
    cursor_x = 0;
    if (cursor_y < scroll_offset)
        scroll_offset = cursor_y;
    else if (cursor_y >= scroll_offset + EDIT_HEIGHT)
        scroll_offset = cursor_y - EDIT_HEIGHT + 1;
    clear_undo();
    return 1;
}
```

In `src/main.c`, replace the `KEY_DELETE` branch with:

```c
        } else if (c == KEY_DELETE) {
            int old_len = 0;
            int old_x = cursor_x;
            int redraw;

            if (mark_active) {
                save_undo_state();
                if (delete_marked_lines())
                    update_cursor();
            } else {
                if (cursor_y < num_lines)
                    old_len = (int)strlen(lines[cursor_y]);
                save_undo_state();
                redraw = delete_char();
                if (redraw)
                    update_cursor();
                else
                    update_current_line(old_len, old_x);
            }
```

- [ ] **Step 4: Rebuild, check the map, run both hardware scripts**

Run: `cmake --build build --target whisper64.prg`

`build/whisper64.map` must show `__bss_end` at or below `$cc00` and `__stack` at `$d000`. If `__bss_end` is past `$cc00`, stop and do not commit.

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c && /tmp/test_page_insert`

Expected: exit 0.

Run: `python3 -u tools/test_mark_delete_c64u.py`

Expected: `mark delete ok` with 441 lines, `mark delete passed`, exit 0.

Run: `python3 -u tools/test_save_roundtrip_c64u.py`

Expected: `save round trip passed`, exit 0. This is the same check as Task 1 on the new binary.

- [ ] **Step 5: Update the docs**

In `AGENTS.md`, after the sentence that ends with `` `delete_char` returns 1 when the screen has to be redrawn. ``, add:

```text
DEL with a mark on removes every whole line from the top of the mark through the bottom and pulls the later pages up once per removed line. The mark can cover only part of a line; the line still goes. DEL with no mark still deletes one character, or joins at column 0. A mark delete clears undo.
```

In `README.md`, add to the Copy/Paste bullet:

```markdown
DEL while lines are marked removes those lines.
```

In `docs/Todo-List.md`, change the mark-mode DEL item from `- [ ]` to `- [x]`.

- [ ] **Step 6: Commit**

```bash
git add include/editor.h src/editor.c src/main.c tools/test_mark_delete_c64u.py AGENTS.md README.md docs/Todo-List.md
git commit -m "$(cat <<'EOF'
Delete the marked lines when DEL is pressed.

A mark is a block of editor lines, and the one-line undo cannot put that block back, so the delete clears the undo after the later pages have been pulled up.
EOF
)"
```

## After both tasks

The controller merges `page-shift-loss` into `main` locally only when Task 1 passed, Task 2 passed, and both reviews are clean. Do not push. A Task 1 failure, a `__bss_end` past `$CC00`, or a failed mark-delete compare stops the merge.
