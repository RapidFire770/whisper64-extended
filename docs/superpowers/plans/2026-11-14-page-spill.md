# Page Spill Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Return on a full 48-line page inserts the new row and carries the displaced last row onto the next page.

**Architecture:** `page_insert_line` shifts one RAM page and reports the displaced row. `new_line` walks later pages through the existing REU and `$W$.P*` load/save, one carried line at a time. `edit_set` / `edit_add` / `edit_add_dec` replace editor `sprintf` so the printf family drops out of the link and the spill still fits under `$CC00`.

**Tech Stack:** C, LLVM-MOS SDK (`c64` platform), host `gcc` for `tools/test_page_insert.c`, `tools/c64u.py` against the C64 Ultimate at `http://192.168.100.193`.

## Global Constraints

- `__bss_end` stays at or below `$CC00`. `__stack` stays `$D000`. Before this work the editor link has `__bss_end` at `$CB5F`.
- The only new BSS is `spill_line` and `spill_next`, 160 bytes, in `src/editor.c`. `page_shift.c` and `editfmt.c` add none. Do not overlay `dir_entries` on `lines[]`. If a rebuild puts `__bss_end` past `$CC00`, stop. Do not raise the stop.
- `sprintf` and `printf` are gone from `src/main.c`, `src/screen.c`, `src/file_ops.c`, `src/editor.c`, `src/goto.c`, and `src/search.c`. The converter files keep `tool_set_str`, `tool_add_str`, and `tool_add_dec`. Do not link `src/conv/toolui.c` into the editor. Do not compile `w64conv`, `w64renum`, or `w64label` at `-Os`.
- After the sprintf task, `build/whisper64.map` does not contain `__udivsi3`. Kilobytes are `(unsigned int)(reu_get_size() >> 10)`.
- `MAX_LINE_LENGTH` is 80. `LINES_PER_PAGE` is 48. Do not divide or take a remainder by `edit_width`.
- `PAGE FULL - MOVE TO END` is gone. The refusal text is `REU FULL`, yellow. Help strings are not lengthened.
- Delete, search, and replace do not walk pages. Goto stays `page = line / 48` and `row = line % 48`.
- Do not save over `DICEROLL-SRC`. The Ultimate save check writes `DICEINS` and leaves it on the disk. Do not run `tools/test_w64conv_c64u.py`. A VICE run is not the hardware check.
- Build the editor with `cmake --build build --target whisper64` from the repo root. If the Ultimate does not answer, stop and ask Josh to bring it back online.
- `shift_rows_40`, `shift_rows_80`, and `shift_viewport` stay `noinline`.

## File map

| File | Responsibility |
|---|---|
| `include/editfmt.h`, `src/editfmt.c` | Editor string and decimal writer. No KERNAL types. |
| `include/page_shift.h`, `src/page_shift.c` | Shift one page. No KERNAL calls. |
| `tools/test_page_insert.c` | Host checks for both of those. |
| `src/main.c`, `src/screen.c`, `src/file_ops.c`, `src/editor.c`, `src/goto.c`, `src/search.c` | Call sites that used `sprintf`. |
| `src/reu.c`, `include/reu.h` | `reu_page_lines` reads a 4-byte header. |
| `src/undo.c`, `include/undo.h` | `clear_undo`. |
| `src/editor.c`, `include/editor.h`, `src/main.c`, `src/clipboard.c` | Return walks pages. Paste stops on refusal. |
| `tools/test_page_insert_c64u.py` | 40-column check of `DICEROLL-SRC`. |
| `CMakeLists.txt` | Both new C files belong to `whisper64.prg` only. |
| `AGENTS.md`, `README.md`, `docs/Todo-List.md` | Behavior, one README sentence, the checked bug. |

---

### Task 1: Host-tested string writer

**Files:**
- Create: `include/editfmt.h`
- Create: `src/editfmt.c`
- Create: `tools/test_page_insert.c`
- Test: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/editfmt.c`

**Interfaces:**
- Consumes: nothing
- Produces: `edit_set`, `edit_add`, `edit_add_n`, `edit_add_dec`, `edit_add_field` with the signatures below. Later tasks call these. `edit_add_dec` divides an `unsigned int` by 10. A C64 `unsigned int` stops at 65535, so the digit buffer is 5 bytes. Host cases stay within 5 digits.

- [ ] **Step 1: Write the failing test**

Create `tools/test_page_insert.c`:

```c
#include "editfmt.h"
#include <stdio.h>
#include <string.h>

static int failed;

static void fail(const char *name) {
    printf("FAIL %s\n", name);
    failed = 1;
}

static void expect_str(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0)
        fail(name);
}

static void test_fmt(void) {
    char buf[64];

    buf[0] = '\0';
    edit_add_dec(buf, 0, 0);
    expect_str("dec 0", buf, "0");

    buf[0] = '\0';
    edit_add_dec(buf, 12, 4);
    expect_str("dec width", buf, "  12");

    buf[0] = '\0';
    edit_add_dec(buf, 12345, 4);
    expect_str("dec long", buf, "12345");

    buf[0] = '\0';
    edit_add_field(buf, "HI", 16);
    expect_str("field hi", buf, "HI              ");

    buf[0] = '\0';
    edit_add_field(buf, "ABCDEFGHIJKLMNOP", 16);
    expect_str("field 16", buf, "ABCDEFGHIJKLMNOP");

    buf[0] = '\0';
    edit_add_n(buf, "ABCDEFGHIJ", 8);
    expect_str("add n", buf, "ABCDEFGH");

    buf[0] = '\0';
    edit_add_n(buf, "AB", 8);
    expect_str("add n short", buf, "AB");

    buf[0] = '\0';
    edit_set(buf, "REU: ");
    edit_add_dec(buf, 16320, 0);
    edit_add(buf, "KB  MAX ");
    edit_add_dec(buf, 4347, 0);
    edit_add(buf, " PAGES");
    expect_str("startup", buf, "REU: 16320KB  MAX 4347 PAGES");
}

int main(void) {
    test_fmt();
    if (failed)
        return 1;
    return 0;
}
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/editfmt.c`

Expected: compile error, `editfmt.h` or `src/editfmt.c` missing.

- [ ] **Step 3: Write the writer**

`include/editfmt.h`:

```c
#ifndef EDITFMT_H
#define EDITFMT_H

void edit_set(char *dst, const char *src);
void edit_add(char *dst, const char *src);
void edit_add_n(char *dst, const char *src, int n);
void edit_add_dec(char *dst, unsigned int value, int width);
void edit_add_field(char *dst, const char *src, int width);

#endif
```

`src/editfmt.c`:

```c
#include "editfmt.h"

void edit_set(char *dst, const char *src) {
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

void edit_add(char *dst, const char *src) {
    while (*dst)
        dst++;
    edit_set(dst, src);
}

void edit_add_n(char *dst, const char *src, int n) {
    while (*dst)
        dst++;
    while (n > 0 && *src) {
        *dst++ = *src++;
        n--;
    }
    *dst = '\0';
}

void edit_add_dec(char *dst, unsigned int value, int width) {
    char rev[5];
    unsigned char n = 0;
    unsigned int v = value;
    int digits;

    while (*dst)
        dst++;
    if (v == 0)
        rev[n++] = '0';
    while (v) {
        rev[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    digits = (int)n;
    while (width > digits) {
        *dst++ = ' ';
        width--;
    }
    while (n)
        *dst++ = rev[--n];
    *dst = '\0';
}

void edit_add_field(char *dst, const char *src, int width) {
    int n = 0;

    while (*dst)
        dst++;
    while (*src) {
        *dst++ = *src++;
        n++;
    }
    while (width > n) {
        *dst++ = ' ';
        n++;
    }
    *dst = '\0';
}
```

- [ ] **Step 4: Run the test and confirm it passes**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/editfmt.c && /tmp/test_page_insert`

Expected: exit 0, no `FAIL` line.

- [ ] **Step 5: Commit**

```bash
git add include/editfmt.h src/editfmt.c tools/test_page_insert.c
git commit -m "$(cat <<'EOF'
Add a small string writer so the editor can drop sprintf.

EOF
)"
```

---

### Task 2: Editor text without sprintf

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/main.c`
- Modify: `src/screen.c`
- Modify: `src/file_ops.c`
- Modify: `src/editor.c`
- Modify: `src/goto.c`
- Modify: `src/search.c`
- Test: `cmake --build build --target whisper64` and the host test from Task 1

**Interfaces:**
- Consumes: `edit_set`, `edit_add`, `edit_add_n`, `edit_add_dec`, `edit_add_field`
- Produces: an editor link with no `sprintf` and no `__udivsi3`. Visible strings stay the bytes in the spec table. `page_info` is 16 bytes.

- [ ] **Step 1: Add the writer to the editor target**

In `CMakeLists.txt`, add `src/editfmt.c` to `SOURCES` after `src/goto.c`. Do not add it to `add_w64_tool`.

- [ ] **Step 2: Replace the startup line**

In `src/main.c`, add `#include "editfmt.h"`. Replace the `sprintf` call:

```c
    if (reu_is_available()) {
        char msg[40];
        unsigned int kb;

        kb = (unsigned int)(reu_get_size() >> 10);
        edit_set(msg, "REU: ");
        edit_add_dec(msg, kb, 0);
        edit_add(msg, "KB  MAX ");
        edit_add_dec(msg, (unsigned int)reu_max_page_count(), 0);
        edit_add(msg, " PAGES");
        show_message(msg, COL_GREEN);
        cgetc();
    }
```

- [ ] **Step 3: Replace the title fields**

In `src/screen.c`, add `#include "editfmt.h"`. Replace `draw_title_pos`:

```c
static void draw_title_pos(void) {
    char pos_info[12];
    int global_line = current_page * LINES_PER_PAGE + cursor_y + 1;
    int n;

    edit_set(pos_info, " ");
    edit_add_dec(pos_info, (unsigned int)global_line, 0);
    edit_add(pos_info, ":");
    edit_add_dec(pos_info, (unsigned int)(cursor_x + 1), 0);
    n = (int)strlen(pos_info);
    while (n < 9)
        pos_info[n++] = ' ';
    pos_info[n] = '\0';
    cputs_at(8, 0, pos_info, COL_GREEN);
}
```

In `redraw_screen`, replace the filename `sprintf`:

```c
    if (current_filename[0] != '\0') {
        title[0] = '\0';
        edit_add_n(title, current_filename, 8);
        cputs_at(0, 0, title, COL_YELLOW);
    } else {
        cputs_at(0, 0, "WHISPER", COL_YELLOW);
    }
```

Replace the drive and page fields. `page_info` is 16 bytes:

```c
    int drive_pos = sw - 10;
    char drive_info[10];
    edit_set(drive_info, " D:");
    edit_add_dec(drive_info, (unsigned int)current_drive, 0);
    cputs_at(drive_pos, 0, drive_info, COL_CYAN);

    int page_pos = sw - 18;
    if (num_pages > 1) {
        char page_info[16];
        edit_set(page_info, " P");
        edit_add_dec(page_info, (unsigned int)(current_page + 1), 0);
        edit_add(page_info, "/");
        edit_add_dec(page_info, (unsigned int)num_pages, 0);
        cputs_at(page_pos, 0, page_info, COL_CYAN);
    }
```

- [ ] **Step 4: Replace the file and directory strings**

In `src/file_ops.c`, add `#include "editfmt.h"`.

Drive prompt and the two `DRIVE=` messages:

```c
    edit_set(msg, "DRIVE (8-15) [NOW:");
    edit_add_dec(msg, (unsigned int)current_drive, 0);
    edit_add(msg, "]: ");
    show_message(msg, COL_YELLOW);
```

Build `DRIVE=` once. Add this above `select_drive` and call it from both successes, the `'8'` to `'9'` branch and the `'1'` then `'0'` to `'5'` branch:

```c
static void show_drive_set(char *msg) {
    edit_set(msg, "DRIVE=");
    edit_add_dec(msg, (unsigned int)current_drive, 0);
    show_message(msg, COL_GREEN);
}
```

Directory title, row, and count:

```c
        edit_set(title, "DIRECTORY - DRIVE ");
        edit_add_dec(title, (unsigned int)current_drive, 0);
        cputs_at(2, 0, title, COL_YELLOW);
```

```c
            edit_add_dec(line, dir_entries[i].blocks, 4);
            edit_add(line, "  ");
            edit_add_field(line, dir_entries[i].name, 16);
            edit_add(line, "  ");
            edit_add(line, dir_entries[i].type);
            cputs_at(1, i - scroll_pos + 4, line, color);
```

`line` is still `char line[40]`. `edit_add_dec` is the first write, and it appends, so set `line[0] = '\0'` before it.

```c
        edit_add_dec(info, (unsigned int)num_dir_entries, 0);
        edit_add(info, " FILES");
        cputs_at(1, 22, info, COL_BLUE);
```

Set `info[0] = '\0'` before `edit_add_dec`.

Load names:

```c
            if (strcmp(dir_entries[selected].type, "SEQ") == 0) {
                edit_set(load_name, dir_entries[selected].name);
                edit_add(load_name, ",S,R");
            } else {
                edit_set(load_name, dir_entries[selected].name);
                edit_add(load_name, ",P,R");
            }
```

Loaded message:

```c
                edit_set(lmsg, "LOADED ");
                edit_add_dec(lmsg, (unsigned int)total_lines, 0);
                edit_add(lmsg, " LINES ");
                edit_add_dec(lmsg, (unsigned int)num_pages, 0);
                edit_add(lmsg, " PAGES");
                show_message(lmsg, COL_GREEN);
```

Save prompt, probe, and write:

```c
        edit_set(msg, "SAVE AS [");
        edit_add(msg, current_filename);
        edit_add(msg, "]: ");
        show_message(msg, COL_YELLOW);
```

```c
    edit_set(full_filename, filename);
    edit_add(full_filename, ",S,R");
```

```c
    edit_set(full_filename, "@0:");
    edit_add(full_filename, filename);
    edit_add(full_filename, ",S,W");
```

Temp read inside the multi-page save. `temp_name` stays 20 bytes. `@0:$W$.P4347,S,W` is 17 characters plus a NUL, so the write form fits. The read form is shorter.

```c
                edit_set(temp_name, TEMP_FILE);
                edit_add(temp_name, ".P");
                edit_add_dec(temp_name, (unsigned int)p, 0);
                edit_add(temp_name, ",S,R");
```

New-file scratch, still `i` from 0 through 9:

```c
        edit_set(msg, "@0:");
        edit_add(msg, TEMP_FILE);
        edit_add(msg, ".P");
        edit_add_dec(msg, (unsigned int)i, 0);
```

- [ ] **Step 5: Replace the temp names, goto text, and replace count**

In `src/editor.c`, add `#include "editfmt.h"`.

```c
    edit_set(temp_name, "@0:");
    edit_add(temp_name, TEMP_FILE);
    edit_add(temp_name, ".P");
    edit_add_dec(temp_name, (unsigned int)current_page, 0);
    edit_add(temp_name, ",S,W");
```

```c
    edit_set(temp_name, TEMP_FILE);
    edit_add(temp_name, ".P");
    edit_add_dec(temp_name, (unsigned int)page_num, 0);
    edit_add(temp_name, ",S,R");
```

In `src/goto.c`, add `#include "editfmt.h"`. Build `LINE n NOT FOUND` once. Add this above `goto_line` and call `show_line_missing(msg, line_num)` from each of the three failures (the `line_num < 0` return, the `page >= num_pages` return, and the `row >= num_lines` return):

```c
static void show_line_missing(char *msg, int line_num) {
    edit_set(msg, "LINE ");
    edit_add_dec(msg, (unsigned int)(line_num + 1), 0);
    edit_add(msg, " NOT FOUND");
    show_message(msg, COL_RED);
}
```

The found-line message is:

```c
    edit_set(msg, "LINE ");
    edit_add_dec(msg, (unsigned int)(line_num + 1), 0);
    show_message(msg, COL_GREEN);
```

In `src/search.c`, add `#include "editfmt.h"`.

```c
        edit_set(msg, "REPLACED ");
        edit_add_dec(msg, (unsigned int)replace_count, 0);
        show_message(msg, COL_GREEN);
```

- [ ] **Step 6: Build and check the map**

Run:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/editfmt.c && /tmp/test_page_insert
rg -n "sprintf\(|printf\(" src/main.c src/screen.c src/file_ops.c src/editor.c src/goto.c src/search.c
cmake --build build --target whisper64
python3 - << 'PY'
import re
text = open("build/whisper64.map").read()
if "sprintf" in text or "__udivsi3" in text:
    raise SystemExit("sprintf or __udivsi3 still linked")
m = re.search(r"([0-9a-f]+)\s+[0-9a-f]+\s+0\s+1\s+__bss_end", text)
addr = int(m.group(1), 16)
print("__bss_end $%04X" % addr)
if addr > 0xCC00:
    raise SystemExit("past $CC00")
PY
```

Expected: host test exit 0, `rg` prints nothing, the build finishes, the Python script prints `__bss_end` at or below `$CC00` and does not exit 1. Leave `#include <stdio.h>` in `include/whisper64.h`.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/main.c src/screen.c src/file_ops.c src/editor.c src/goto.c src/search.c
git commit -m "$(cat <<'EOF'
Build editor text without sprintf so the printf family leaves the link.

EOF
)"
```

---

### Task 3: Host-tested page shift

**Files:**
- Create: `include/page_shift.h`
- Create: `src/page_shift.c`
- Modify: `tools/test_page_insert.c`
- Modify: `CMakeLists.txt`
- Test: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c`

**Interfaces:**
- Consumes: nothing from the editor
- Produces:

```c
int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced);
```

`*count` below 48: `row` is 0 through `*count`, the return is 0, `displaced` is not written. `*count` is 48: `row` is 0 through 47, the old last row is copied to `displaced` first, the return is 1, `*count` stays 48. The copy stores at most 79 characters and a NUL. `text` is not a pointer into `page`.

- [ ] **Step 1: Add the failing page cases**

Put `#include "page_shift.h"` with the includes at the top of `tools/test_page_insert.c`. Add `test_pages()` below `test_fmt()`, and call it from `main` before the `failed` check. Keep `test_fmt()`.

```c

static void fill_numbered(char page[][MAX_LINE_LENGTH], int n) {
    int i;
    for (i = 0; i < n; i++) {
        page[i][0] = 'L';
        page[i][1] = (char)('0' + (i / 10));
        page[i][2] = (char)('0' + (i % 10));
        page[i][3] = '\0';
    }
}

static int displaced_untouched(const unsigned char *d) {
    int i;
    for (i = 0; i < MAX_LINE_LENGTH; i++) {
        if (d[i] != 0xA5)
            return 0;
    }
    return 1;
}

static void test_pages(void) {
    char page[LINES_PER_PAGE][MAX_LINE_LENGTH];
    unsigned char displaced[MAX_LINE_LENGTH];
    int count;
    int rc;
    char big[81];
    int i;

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    strcpy(page[2], "CC");
    count = 3;
    memset(displaced, 0xA5, sizeof displaced);
    rc = page_insert_line(page, &count, 1, "XX", (char *)displaced);
    if (rc != 0 || count != 4 || strcmp(page[0], "AA") != 0 ||
        strcmp(page[1], "XX") != 0 || strcmp(page[2], "BB") != 0 ||
        strcmp(page[3], "CC") != 0 || !displaced_untouched(displaced))
        fail("insert middle");

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    strcpy(page[2], "CC");
    count = 3;
    memset(displaced, 0xA5, sizeof displaced);
    rc = page_insert_line(page, &count, 3, "XX", (char *)displaced);
    if (rc != 0 || count != 4 || strcmp(page[0], "AA") != 0 ||
        strcmp(page[1], "BB") != 0 || strcmp(page[2], "CC") != 0 ||
        strcmp(page[3], "XX") != 0 || !displaced_untouched(displaced))
        fail("append");

    memset(page, 0, sizeof page);
    strcpy(page[0], "AA");
    strcpy(page[1], "BB");
    count = 2;
    rc = page_insert_line(page, &count, 1, "", (char *)displaced);
    if (rc != 0 || count != 3 || strcmp(page[0], "AA") != 0 ||
        page[1][0] != '\0' || strcmp(page[2], "BB") != 0)
        fail("blank");

    memset(page, 0, sizeof page);
    fill_numbered(page, LINES_PER_PAGE);
    count = LINES_PER_PAGE;
    rc = page_insert_line(page, &count, 0, "NEW", (char *)displaced);
    if (rc != 1 || count != LINES_PER_PAGE || strcmp(page[0], "NEW") != 0 ||
        strcmp(page[1], "L00") != 0 || strcmp((char *)displaced, "L47") != 0)
        fail("full head");

    memset(page, 0, sizeof page);
    fill_numbered(page, LINES_PER_PAGE);
    count = LINES_PER_PAGE;
    rc = page_insert_line(page, &count, 47, "NEW", (char *)displaced);
    if (rc != 1 || count != LINES_PER_PAGE || strcmp(page[47], "NEW") != 0 ||
        strcmp((char *)displaced, "L47") != 0)
        fail("full last");
    for (i = 0; i < 47; i++) {
        char want[4];
        want[0] = 'L';
        want[1] = (char)('0' + (i / 10));
        want[2] = (char)('0' + (i % 10));
        want[3] = '\0';
        if (strcmp(page[i], want) != 0)
            fail("full last kept");
    }

    memset(page, 0, sizeof page);
    memset(big, 'A', 80);
    big[80] = '\0';
    count = 1;
    page[0][0] = '\0';
    rc = page_insert_line(page, &count, 0, big, (char *)displaced);
    if (rc != 0 || count != 2 || strlen(page[0]) != 79)
        fail("truncate");
    for (i = 0; i < 79; i++) {
        if (page[0][i] != 'A')
            fail("truncate bytes");
    }
}
```

Add `#include <string.h>` if the file does not already include it. Task 1's file already includes it.

- [ ] **Step 2: Run the test and confirm it fails**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c`

Expected: compile error, `page_shift.h` missing or `page_insert_line` undefined.

- [ ] **Step 3: Write the shift**

`include/page_shift.h`:

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

#endif
```

`src/page_shift.c`:

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

int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced) {
    if (*count < LINES_PER_PAGE) {
        memmove(page[row + 1], page[row],
                (size_t)(*count - row) * MAX_LINE_LENGTH);
        copy_text(page[row], text);
        (*count)++;
        return 0;
    }

    copy_text(displaced, page[LINES_PER_PAGE - 1]);
    if (row < LINES_PER_PAGE - 1) {
        memmove(page[row + 1], page[row],
                (size_t)(LINES_PER_PAGE - 1 - row) * MAX_LINE_LENGTH);
    }
    copy_text(page[row], text);
    return 1;
}
```

This file includes `page_shift.h` and `string.h` only. Add `src/page_shift.c` to `SOURCES` in `CMakeLists.txt`. Do not add it to a tool target.

- [ ] **Step 4: Run the test and confirm it passes**

Run: `gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c && /tmp/test_page_insert`

Expected: exit 0, no `FAIL` line.

Also run `cmake --build build --target whisper64`. Expected: the build finishes. `__bss_end` stays at or below `$CC00` because this file has no BSS. Use the Python map check from Task 2.

- [ ] **Step 5: Commit**

```bash
git add include/page_shift.h src/page_shift.c tools/test_page_insert.c CMakeLists.txt
git commit -m "$(cat <<'EOF'
Shift one editor page in a host-tested function.

EOF
)"
```

---

### Task 4: Carry a full page forward

**Files:**
- Modify: `include/editor.h`
- Modify: `src/editor.c`
- Modify: `include/reu.h`
- Modify: `src/reu.c`
- Modify: `include/undo.h`
- Modify: `src/undo.c`
- Modify: `src/main.c`
- Modify: `src/clipboard.c`
- Test: host test from Task 3, then `cmake --build build --target whisper64`

**Interfaces:**
- Consumes: `page_insert_line`, `reu_is_available`, `reu_max_page_count`, `load_page`, `save_current_page_to_temp`, `show_message`, `COL_YELLOW`
- Produces: `int new_line(void)` returns 1 when a row was inserted and 0 when it refused. `int reu_page_lines(int page_num)` returns the stored count, or 0 when the REU is absent, the page is out of range, the magic is not `$C64E`, or the count is 0 or greater than 48. `void clear_undo(void)` clears both flags and does not copy line bytes.

- [ ] **Step 1: Add the header read and the undo clear**

In `include/reu.h`, next to `reu_load_page`:

```c
int reu_page_lines(int page_num);
```

In `src/reu.c`, next to `reu_load_page`. This uses the existing `REUPageHeader`, `reu_page_addr`, and `reu_read`. It does not write `lines[]`.

```c
int reu_page_lines(int page_num) {
    REUPtr addr;
    REUPageHeader header;

    if (!reu_available)
        return 0;
    if (page_num < 0 || page_num >= reu_max_pages)
        return 0;

    addr = reu_page_addr(page_num);
    reu_read(addr, &header, sizeof header);
    if (header.magic != REU_PAGE_MAGIC)
        return 0;
    if (header.num_lines_stored == 0 ||
        header.num_lines_stored > LINES_PER_PAGE)
        return 0;
    return (int)header.num_lines_stored;
}
```

In `include/undo.h`:

```c
void clear_undo(void);
```

In `src/undo.c`:

```c
void clear_undo(void) {
    undo_available = 0;
    redo_available = 0;
}
```

- [ ] **Step 2: Change the Return signature and the callers**

In `include/editor.h`, replace `void new_line(void);` with `int new_line(void);`.

In `src/main.c`, the Return key becomes:

```c
        } else if (c == KEY_RETURN) {
            if (num_lines < LINES_PER_PAGE)
                save_undo_state();
            if (new_line())
                update_cursor();
```

In `src/clipboard.c`, include `undo.h`. Count how many characters `insert_char` actually stored on that clipboard line. If the following `new_line` returns 0, delete those characters before the cursor and leave the undo flags alone. If it returns 1, call `clear_undo` so an older snapshot cannot restore the pre-insert line count. The loop becomes:

```c
        int inserted = 0;

        for (j = 0; clipboard[i][j]; j++) {
            int column = cursor_x;

            insert_char(clipboard[i][j]);
            if (cursor_x > column)
                inserted++;
        }
        if (i < clipboard_lines - 1) {
            if (!new_line()) {
                while (inserted > 0) {
                    delete_char();
                    inserted--;
                }
                update_cursor();
                show_message("REU FULL", COL_YELLOW);
                return;
            }
            clear_undo();
        }
```

- [ ] **Step 3: Replace `new_line`**

In `src/editor.c`, add `#include "page_shift.h"` and `#include "undo.h"`. Delete `create_new_page`. Add the two buffers at file scope:

```c
static char spill_line[MAX_LINE_LENGTH];
static char spill_next[MAX_LINE_LENGTH];
```

Replace `new_line` with this. The tail is the bytes from the cursor through the end of the row. A full page is checked before any of those bytes are written back into `lines[]`.

```c
static void copy_tail(char *dst, const char *src) {
    int i = 0;

    while (src[i] && i < MAX_LINE_LENGTH - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int spill_blocked(void) {
    int p;

    if (!reu_is_available())
        return 0;
    for (p = current_page + 1; p < num_pages; p++) {
        int n = reu_page_lines(p);

        if (n > 0 && n < LINES_PER_PAGE)
            return 0;
    }
    return num_pages >= reu_max_page_count();
}

static void open_carried_page(const char *text) {
    memset(lines, 0, sizeof lines);
    copy_tail(lines[0], text);
    num_lines = 1;
    num_pages = current_page + 1;
    page_modified = 1;
}

static int push_spill(void) {
    int page = current_page + 1;
    int home = -1;

    for (;;) {
        if (page >= num_pages) {
            save_current_page_to_temp();
            current_page = page;
            open_carried_page(spill_line);
            save_current_page_to_temp();
            if (home < 0)
                home = page;
            return home;
        }
        load_page(page);
        if (reu_is_available() && reu_page_lines(page) == 0) {
            page++;
            continue;
        }
        if (home < 0)
            home = page;
        if (page_insert_line(lines, &num_lines, 0, spill_line, spill_next)) {
            copy_tail(spill_line, spill_next);
            page_modified = 1;
            page++;
            continue;
        }
        page_modified = 1;
        return home;
    }
}

int new_line(void) {
    int origin;
    int follow;
    int home;

    copy_tail(spill_line, &lines[cursor_y][cursor_x]);
    if (num_lines < LINES_PER_PAGE) {
        lines[cursor_y][cursor_x] = '\0';
        page_insert_line(lines, &num_lines, cursor_y + 1, spill_line, spill_next);
        total_lines++;
        cursor_y++;
        cursor_x = 0;
        page_modified = 1;
        if (cursor_y - scroll_offset >= EDIT_HEIGHT)
            scroll_offset++;
        return 1;
    }

    if (spill_blocked()) {
        show_message("REU FULL", COL_YELLOW);
        return 0;
    }

    origin = current_page;
    follow = cursor_y == num_lines - 1;
    lines[cursor_y][cursor_x] = '\0';
    if (!follow) {
        page_insert_line(lines, &num_lines, cursor_y + 1, spill_line, spill_next);
        copy_tail(spill_line, spill_next);
        cursor_y++;
        cursor_x = 0;
        if (cursor_y - scroll_offset >= EDIT_HEIGHT)
            scroll_offset++;
    }
    page_modified = 1;
    total_lines++;
    mark_active = 0;
    home = push_spill();
    clear_undo();
    if (follow) {
        if (current_page != home)
            load_page(home);
        cursor_x = 0;
        cursor_y = 0;
        scroll_offset = 0;
    } else if (current_page != origin) {
        load_page(origin);
    }
    return 1;
}
```

`open_carried_page` does not change the cursor. `push_spill` does not either. `load_page` of the page already in memory returns without discarding it, which is the existing check at the top of `load_page`. There is no assignment of `num_pages` from `total_lines`.

- [ ] **Step 4: Build and re-run the host test**

Run:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert tools/test_page_insert.c src/page_shift.c src/editfmt.c && /tmp/test_page_insert
cmake --build build --target whisper64
python3 - << 'PY'
import re
text = open("build/whisper64.map").read()
if "sprintf" in text or "__udivsi3" in text:
    raise SystemExit("sprintf or __udivsi3 still linked")
m = re.search(r"([0-9a-f]+)\s+[0-9a-f]+\s+0\s+1\s+__bss_end", text)
addr = int(m.group(1), 16)
stack = re.search(r"__stack = (0x[0-9a-fA-F]+)", text)
print("__bss_end $%04X" % addr)
print(stack.group(0))
if addr > 0xCC00:
    raise SystemExit("past $CC00")
if stack.group(1).lower() != "0xd000":
    raise SystemExit("stack moved")
PY
rg -n "PAGE FULL" src include
```

Expected: host test exit 0, build finishes, `__bss_end` at or below `$CC00`, `__stack = 0xD000`, `rg` prints nothing. If `__bss_end` is past `$CC00`, stop. Do not overlay the directory and do not raise the stop.

- [ ] **Step 5: Commit**

```bash
git add include/editor.h src/editor.c include/reu.h src/reu.c include/undo.h src/undo.c src/main.c src/clipboard.c
git commit -m "$(cat <<'EOF'
Carry a full page's last line onto the next page.

EOF
)"
```

---

### Task 5: Ultimate check and the docs

**Files:**
- Create: `tools/test_page_insert_c64u.py`
- Modify: `AGENTS.md`
- Modify: `README.md`
- Modify: `docs/Todo-List.md`
- Test: `python3 tools/test_page_insert_c64u.py`

**Interfaces:**
- Consumes: `tools/c64u.py` `reset`, `load_and_run`, `PRG`, `inject_keys`, `read_mem`, `SCREEN`, `COLOR`, `dump_rows`, and the key constants already in that file. F2 is PETSCII 137. CTRL+N is 14. CTRL+G is 7. Those match `include/whisper64.h`.
- Produces: a script that prints `ok` and no `FAIL` line. `DICEINS` remains on drive 8. `DICEROLL-SRC` is unchanged.

- [ ] **Step 1: Write the hardware script**

Create `tools/test_page_insert_c64u.py`. Run it from the repo root. It resets once, checks drive 8, then loads the editor. Each case loads `DICEROLL-SRC` again through F1. Screen columns 3 through 39 are compared after masking off bit 7, so the cursor's reverse cell still counts as a space on a blank row. Gutter digits are columns 0 and 1. The title line number is columns 8 through 16 of row 0. The page field starts at column 22.

```python
#!/usr/bin/env python3
"""40-column check that Return on a full page carries the last row forward."""

import importlib.util
import json
import subprocess
import sys
import time

spec = importlib.util.spec_from_file_location("c64drv", "tools/c64u.py")
drv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(drv)

K_F2 = 137
K_CTRL_N = 14
K_CTRL_G = 7
BLANK = b" " * 37


def fail(msg):
    print("FAIL", msg)
    drv.dump_rows(label="fail")
    sys.exit(1)


def row_bytes(scr, row):
    return bytes(scr[row * 40:(row + 1) * 40])


def wait_row(row, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        scr = drv.read_mem(drv.SCREEN, 1000)
        if needle in row_bytes(scr, row):
            return True
        time.sleep(0.3)
    return False


def wait_status(needles, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        scr = drv.read_mem(drv.SCREEN, 1000)
        row = row_bytes(scr, 24)
        for needle in needles:
            if needle in row:
                return needle
        time.sleep(0.3)
    return None


def ensure_w64():
    raw = subprocess.check_output(["c64u", "drives", "list", "--json"], text=True)
    data = json.loads(raw)
    found = None
    for item in data:
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
            "c64u", "drives", "mount", letter, "USB1/MY FILES/W64.d64"])
        time.sleep(1)
        return
    fail("drive 8 has a different image: " + image)


def boot():
    drv.reset()
    ensure_w64()
    drv.load_and_run(drv.PRG)
    scr = drv.read_mem(drv.SCREEN, 1000)
    if b"REU" in row_bytes(scr, 24):
        drv.inject_keys([0x20], settle=0.6)


def cursor_row(scr):
    for i, b in enumerate(scr):
        if b & 0x80:
            return i // 40
    return -1


def title_line(scr):
    raw = bytes(b & 0x7F for b in scr[8:17])
    num = ""
    for ch in raw.decode("latin1"):
        if ch == ":":
            break
        if ch.isdigit():
            num += ch
    if not num:
        return -1
    return int(num)


def page_pair(scr):
    raw = bytes(b & 0x7F for b in scr[22:40]).decode("latin1")
    if "P" not in raw or "/" not in raw:
        return None
    body = raw[raw.index("P") + 1:]
    left, right = body.split("/", 1)

    def digits(s):
        out = ""
        for ch in s:
            if ch.isdigit():
                out += ch
            elif out:
                break
        return int(out) if out else None

    a = digits(left)
    b = digits(right)
    if a is None or b is None:
        return None
    return a, b


def line_text(scr):
    r = cursor_row(scr)
    if r < 0:
        fail("no cursor")
    return bytes((scr[r * 40 + c] & 0x7F) for c in range(3, 40))


def gutter(scr):
    r = cursor_row(scr)
    if r < 0:
        fail("no cursor")
    return bytes((scr[r * 40 + c] & 0x7F) for c in (0, 1))


def screen():
    return drv.read_mem(drv.SCREEN, 1000)


def goto_line(n):
    codes = [K_CTRL_G] + [ord(ch) for ch in str(n)] + [drv.K_RETURN]
    drv.inject_keys(codes, settle=0.4)
    deadline = time.time() + 5
    while time.time() < deadline:
        scr = screen()
        if title_line(scr) == n:
            return scr
        time.sleep(0.2)
    fail("goto %d" % n)


def read_line(n):
    return line_text(goto_line(n))


def selected_row(col):
    for r in range(4, 22):
        if (col[r * 40 + 1] & 0x0F) == 5:
            return r
    return -1


def load_named(name):
    drv.inject_keys([drv.K_F1], settle=0.5)
    if not wait_row(0, b"DIRECTORY", 30):
        if wait_row(24, b"NO FILES", 1) or wait_row(24, b"DIR ERROR", 1):
            fail("directory")
        fail("directory timeout")
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


def save_diceins():
    drv.inject_keys([K_F2], settle=0.5)
    if not wait_row(24, b"SAVE AS", 10):
        fail("save prompt")
    drv.inject_keys([drv.K_DELETE] * 12, settle=0.35)
    drv.inject_keys(list(b"DICEINS") + [drv.K_RETURN], settle=0.5)
    hit = wait_status([b"OVERWRITE", b"SAVED"], 30)
    if hit == b"OVERWRITE":
        drv.inject_keys([ord("Y")], settle=0.5)
        hit = wait_status([b"SAVED"], 30)
    if hit != b"SAVED":
        fail("saved")


def press_return():
    drv.inject_keys([drv.K_RETURN], settle=0.8)


def case_middle():
    load_named("DICEROLL-SRC")
    a = read_line(1)
    b = read_line(48)
    c = read_line(49)
    goto_line(1)
    press_return()
    if read_line(1) != BLANK:
        fail("middle line 1")
    if read_line(2) != a:
        fail("middle line 2")
    if read_line(49) != b:
        fail("middle line 49")
    if read_line(50) != c:
        fail("middle line 50")
    save_diceins()
    load_named("DICEINS")
    if read_line(1) != BLANK or read_line(2) != a:
        fail("reloaded DICEINS")


def case_last_row():
    load_named("DICEROLL-SRC")
    a = read_line(48)
    b = read_line(49)
    goto_line(48)
    drv.inject_keys([drv.K_RIGHT] * 79, settle=0.35)
    press_return()
    scr = screen()
    if title_line(scr) != 49 or line_text(scr) != BLANK:
        fail("last-row cursor")
    if read_line(48) != a:
        fail("last-row kept")
    if read_line(50) != b:
        fail("last-row pushed")


def to_last_page():
    scr = screen()
    prev = page_pair(scr)
    for _ in range(20):
        drv.inject_keys([K_CTRL_N], settle=0.4)
        scr = screen()
        cur = page_pair(scr)
        if cur == prev:
            return scr
        prev = cur
    fail("last page")


def to_last_row(scr):
    prev = gutter(scr)
    for _ in range(48):
        drv.inject_keys([drv.K_DOWN], settle=0.25)
        scr = screen()
        cur = gutter(scr)
        if cur == prev:
            return scr
        prev = cur
    fail("last row")


def case_end():
    load_named("DICEROLL-SRC")
    scr = to_last_row(to_last_page())
    digits = gutter(scr)
    before = page_pair(scr)
    if digits != b"47":
        for _ in range(48):
            if gutter(scr) == b"00":
                break
            drv.inject_keys([drv.K_UP], settle=0.25)
            scr = screen()
        press_return()
        scr = screen()
        if page_pair(scr) != before:
            fail("short page grew")
        return
    text = line_text(scr)
    number = title_line(scr)
    drv.inject_keys([drv.K_RIGHT] * 79, settle=0.35)
    press_return()
    scr = screen()
    after = page_pair(scr)
    if after is None or before is None or after[1] != before[1] + 1:
        fail("new page count")
    if gutter(scr) != b"00" or line_text(scr) != BLANK:
        fail("new page row")
    if read_line(number) != text:
        fail("old last row")


def main():
    boot()
    case_middle()
    case_last_row()
    case_end()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it**

Run: `python3 tools/test_page_insert_c64u.py`

Expected: the last line is `ok`, and there is no `FAIL` line. Drive 8 still has `W64.d64`. `DICEINS` is on that disk. `DICEROLL-SRC` was not the save name. A `find DICEROLL-SRC` failure means the editor's 39-entry directory list did not contain that name. Stop and tell Josh. Do not raise `MAX_DIR_ENTRIES`.

If the connection fails, stop and ask Josh. Do not check off the todo and do not commit this task.

- [ ] **Step 3: Update the docs**

In `AGENTS.md`, replace the sentence that starts `` `new_line` can open the next page only `` with:

```text
`new_line` inserts the tail after the cursor as the next row. On a full page the last row is carried to row 0 of the next page, and a full page carries its own last row onward. A new page is added only when the carried row falls off the last page. Return on the last row follows that tail to row 0 of the page that received it. Return on an earlier row stays on the current page. A stored count of 0 is not an open page. When the REU is present, no later page has a stored count from 1 to 47, and `num_pages` is already the REU page limit, Return shows `REU FULL` and changes nothing. That refusal leaves the previous undo alone. A spill clears undo. Return on a short page saves undo before the cut. A paste that inserts a row clears that snapshot. A refused paste leaves it. A paste that cannot open the next row deletes the characters it just stored on the current line. `num_pages` is the number of stored pages. `new_line` does not rebuild it from `total_lines`. The clipboard stores two lines.
```

Keep the following sentence, `copy_marked` stops at 2. In the sentence that currently says the editor link has `__bss_end` at `$CB5F` and `__heap_start` at `$CB7F`, replace those two addresses with the values from the map printed in Task 4. `__stack` stays `$D000`.

In `README.md`, replace the multi-page feature bullet with:

```markdown
- **Multi-Page Editing**: Pages stored in REU or disk temp files. Return on a full page pushes the last line onto the next page.
```

In `docs/Todo-List.md`, check the page-full item and add the note:

```markdown
- [x] When trying to edit a long multipage file and hitting enter it cannot add a new empty line and push the others down. The program just gives error "PAGE FULL - MOVE TO END". This is a breaking bug when trying to edit a long program or text document and you need to add new lines above old ones.
  Return on a full page carries the last row to the next page. Checked on the Ultimate with DICEROLL-SRC. DICEINS was left on the disk.
```

Do not lengthen help strings.

- [ ] **Step 4: Commit**

```bash
git add tools/test_page_insert_c64u.py AGENTS.md README.md docs/Todo-List.md
git commit -m "$(cat <<'EOF'
Record the full-page Return spill after the Ultimate check.

EOF
)"
```
