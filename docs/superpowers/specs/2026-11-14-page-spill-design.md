# Return on a full page spills one line

Return inserts a row on a 48-line page by carrying the displaced last row to row 0 of the next page. A full later page carries its own last row onward. A new page is added only when that row falls off the last page. The screen, the gutter, CTRL+N, CTRL+B, and CTRL+G stay page-based. Delete does not pull a line back from the next page.

The editor link has `__bss_end` at `$CB5F`. New editor code has to leave `__bss_end` at or below `$CC00` and `__stack` at `$D000`. The bytes come from dropping `sprintf` in the editor. The converter programs are unchanged.

## Insert

`page_insert_line` lives in `src/page_shift.c` and is declared in `include/page_shift.h`. That header does not include `whisper64.h`. It defines `MAX_LINE_LENGTH` (80) and `LINES_PER_PAGE` (48) only when the includer has not defined them. `src/page_shift.c` includes `page_shift.h` and nothing from the KERNAL. `CMakeLists.txt` adds `src/page_shift.c` to `whisper64.prg` only.

```c
int page_insert_line(char page[][MAX_LINE_LENGTH], int *count,
                     int row, const char *text, char *displaced);
```

`page` holds `LINES_PER_PAGE` rows. `*count` is the live row count, from 0 through 48. `text` is not a pointer into `page`. `displaced` is a buffer of `MAX_LINE_LENGTH` bytes when `*count` is 48, and is unused otherwise. The copy stores at most 79 characters and a NUL.

When `*count` is below 48, `row` is from 0 through `*count`. Rows at `row` and after move up one, `text` is stored at `row`, `*count` grows by one, `displaced` is not written, and the return value is 0. Moving onto `page[*count]` is in range because that row was free.

When `*count` is 48, `row` is from 0 through 47. The function copies `page[47]` into `displaced` first, moves rows `row` through 46 up one, stores `text` at `row`, leaves `*count` at 48, and returns 1. `row` 47 replaces the last row; the old last row exists only in `displaced`.

`new_line` returns 1 when a row was inserted and 0 when it refused. `src/editor.c` holds two file-scope 80-byte buffers, `spill_line` and `spill_next`. They are the carry buffers. They are not locals, so `load_page` is not stacked on top of them.

A page with a free row copies the bytes from the cursor through the end of the row into `spill_line`, writes a NUL at the cursor, and calls `page_insert_line` at `cursor_y + 1`. `total_lines` grows by one. The cursor moves to that new row, column 0. The scroll rule already in `new_line` stays. `page_modified` is set. `num_pages` is not changed. The mark is left as it was. Return saves undo before this cut. A paste that inserts a row clears the previous snapshot, because that snapshot's line count would drop the last row. A refused paste leaves it. `clear_undo` is not called on the short-page Return path.

A full page does the capacity check below before it changes the buffer. Then:

| Cursor row | This page | Carried text | Cursor afterward |
|---|---|---|---|
| Any row except the last | Tail inserted under the cursor. The old last row is what leaves. The page stays 48 lines. | Inserted at row 0 of the first page that accepts it. | Stays on the new row. Column 0. The existing scroll rule applies. |
| Last row | The row keeps the text before the cursor. | The tail, including an empty tail. Inserted at row 0 of the first page that accepts it. | That tail. That page, row 0, column 0, scroll 0. |

`total_lines` grows by one for the row Return created. A carried row is the same line on another page. `page_modified` is set on the current page. A spill clears `mark_active`.

The walk starts at the next page. `load_page` saves the page being left. Each existing page receives the carried text at row 0 through `page_insert_line`. A return of 0 ends the walk. A return of 1 copies `spill_next` back to `spill_line` and continues. A carried line that falls off the last page is stored as a new page: one row, that text, `num_pages` one greater, then saved. The walk does not recurse.

The new row is not carried past the page where Return put it. On a last-row Return that page is the first page that accepted the tail, at its row 0, even when that page was full and had to carry its own old last row further. A stored count of 0 is skipped, so that page is the next index only when the next page has a real count. On any earlier row the new row stays on the page where Return was pressed.

After the walk, `load_page` brings back the page the cursor belongs on when the walk stopped somewhere else. `load_page` of the page already in memory returns without discarding it. `main` redraws only after a successful `new_line`, and `lines[]` has to be that page by then.

`create_new_page` is removed. Nothing else calls it. `num_pages` stays the count of stored pages. The assignment at the bottom of `new_line` that rebuilds `num_pages` from `total_lines` is removed. An insert on a short page does not drop the pages that follow it.

Delete, search, and replace do not gain a cross-page walk. Goto keeps `page = line / 48` and `row = line % 48`.

## REU capacity

The check runs only when the current page is full and `reu_is_available()` is true, and it runs before any edit. `reu_page_lines(int page_num)`, declared in `include/reu.h` and defined in `src/reu.c`, reads the 4-byte page header and does not write `lines[]` or change `current_page`. It returns the stored line count. It returns 0 when the REU is absent, the page is out of range, the magic is not `$C64E`, or the stored count is 0 or greater than 48.

The check looks at each page from the next one through `num_pages - 1`. A stored count from 1 to 47 means the walk will stop there. A count of 0 is not an open page. If none of those pages has a count from 1 to 47, the walk needs one new page. When `num_pages >= reu_max_page_count()`, `new_line` shows `REU FULL` in yellow and returns 0. It does not call `clear_undo`. The buffer, the cursor, `total_lines`, `num_pages`, `page_modified`, and the undo flags stay as they were.

A page whose stored count is 0 is skipped during the walk. The carried line is inserted on the next page that has a real count, or on a new page when the carry falls off the end. Return on the last row follows that carried line, which is the first page that accepted it.

With no REU, there is no free-space check. The same walk writes `$W$.P*` through the existing temp-file load and save. A failed temp-file open stays the failure those functions already have.

## Undo, paste, and the key loop

`clear_undo` is declared in `include/undo.h` and defined in `src/undo.c`. It sets the undo and redo flags to 0. It does not copy line bytes.

Return on a short page calls `save_undo_state` before `new_line`. A paste does not save one. After `new_line` returns 1, the paste calls `clear_undo`, so an older snapshot cannot restore the pre-insert line count. A 0 return leaves the flags alone. On a 0 return `main` does not call `update_cursor`, so the status row keeps `REU FULL`. On a 1 return it calls `update_cursor`. A spill that touched another page calls `clear_undo` after the walk, because the one stored line cannot restore the other pages. A refusal leaves the existing undo flags alone.

`paste_clipboard` still calls `new_line` between clipboard lines. A 0 return deletes the characters just stored on the current line, redraws with `update_cursor`, shows `REU FULL` again, and returns without `TEXT PASTED`.

## Strings without sprintf

`sprintf` is removed from `src/main.c`, `src/screen.c`, `src/file_ops.c`, `src/editor.c`, `src/goto.c`, and `src/search.c`. The editor does not call `printf` or `sprintf`. The converter's `tool_set_str`, `tool_add_str`, and `tool_add_dec` stay in `src/conv/toolui.c` and are not linked into the editor.

`src/editfmt.c` and `include/editfmt.h` are editor-only. The header pulls in no KERNAL types. `CMakeLists.txt` adds the file to `whisper64.prg` only.

```c
void edit_set(char *dst, const char *src);
void edit_add(char *dst, const char *src);
void edit_add_n(char *dst, const char *src, int n);
void edit_add_dec(char *dst, unsigned int value, int width);
void edit_add_field(char *dst, const char *src, int width);
```

`edit_set` replaces the destination. `edit_add` appends. `edit_add_n` appends at most `n` characters and stops at the NUL when `src` is shorter. `edit_add_dec` appends the decimal digits of `value`, and `0` for zero. A `width` above 0 adds leading spaces when the digits are shorter than `width`. A longer number is written in full. `width` 0 adds no spaces. The divide is on an `unsigned int`, by 10, the same way `tool_add_dec` does it. `edit_add_field` appends `src` and, when `width` is above 0 and `src` is shorter, trailing spaces out to `width`. A longer `src` is copied in full. `width` 0 appends `src` with no padding.

The visible text stays the same bytes as the `sprintf` calls they replace:

| Site | Text |
|---|---|
| Startup, `main` | `REU: ` + kilobytes + `KB  MAX ` + page count + ` PAGES` |
| `draw_title_pos` | space + line + `:` + column, then spaces out to 9 characters |
| Title name | first 8 characters of the filename |
| Title drive | ` D:` + drive |
| Title page, only when `num_pages` is greater than 1 | ` P` + current page (1-based) + `/` + `num_pages` |
| Drive prompt | `DRIVE (8-15) [NOW:` + drive + `]: ` |
| Drive chosen | `DRIVE=` + drive |
| Directory title | `DIRECTORY - DRIVE ` + drive |
| Directory row | blocks in 4 columns, `  `, name in 16 columns, `  `, type |
| Directory count | count + ` FILES` |
| Load name | filename + `,S,R` or `,P,R` |
| After load | `LOADED ` + `total_lines` + ` LINES ` + `num_pages` + ` PAGES` |
| Save prompt | `SAVE AS [` + filename + `]: ` |
| Save probe | filename + `,S,R` |
| Save write | `@0:` + filename + `,S,W` |
| Temp read | `$W$` + `.P` + page + `,S,R` |
| Temp write | `@0:` + `$W$` + `.P` + page + `,S,W` |
| New-file scratch | `@0:` + `$W$` + `.P` + n, for n from 0 through 9 |
| Missing goto | `LINE ` + number + ` NOT FOUND` |
| Found goto | `LINE ` + number |
| Replace count | `REPLACED ` + count |

Kilobytes are `(unsigned int)(reu_get_size() >> 10)`. The largest probe result is `16320`, which fits in 16 bits. The startup line does not divide a 32-bit value. After the link, `build/whisper64.map` does not contain `__udivsi3`. A remaining 32-bit divide is replaced until that symbol is gone.

`page_info` in `redraw_screen` grows from 10 bytes to 16 so ` P4347/4347` fits. The other stack buffers stay the sizes they have. The 9-column pad in `draw_title_pos` stays there. The scratch loop still covers ten temp names.

`PAGE FULL - MOVE TO END` is gone. Help strings are not lengthened.

## Link

`__bss_end` is at or below `$CC00`. `__stack` stays `$D000`. The two spill buffers are the only new BSS, 160 bytes. `page_shift.c` and `editfmt.c` add none. The directory array is not overlaid on `lines[]`. If the map is past `$CC00`, the change stops. The directory overlay is not the fallback, and the `$CC00` stop is not raised.

## Tests

Host check, from the repo root:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_page_insert \
    tools/test_page_insert.c src/page_shift.c src/editfmt.c
/tmp/test_page_insert
```

Exit 0, and the output has no `FAIL` line. Each `edit_*` case starts from an empty destination. The page cases use the rows `L00` through `L47` (`L00` at row 0) except where a case names other text.

- Rows `AA`, `BB`, `CC`, insert `XX` at row 1. The rows are `AA`, `XX`, `BB`, `CC`. The count is 4. The return is 0. A `displaced` buffer filled with `$A5` is unchanged.
- Rows `AA`, `BB`, `CC`, insert `XX` at row 3. The rows are `AA`, `BB`, `CC`, `XX`. The count is 4. The return is 0. `displaced` is unchanged.
- Rows `AA`, `BB`, insert an empty string at row 1. Row 1 is a NUL at column 0. The rows around it are `AA` and `BB`. The count is 3. The return is 0.
- 48 rows, insert `NEW` at row 0. The return is 1. `displaced` is `L47`. Row 0 is `NEW`. Row 1 is `L00`. The count stays 48.
- 48 rows, insert `NEW` at row 47. The return is 1. `displaced` is `L47`. Row 47 is `NEW`. Rows 0 through 46 are `L00` through `L46`.
- `text` of 80 `A` characters stores 79 `A` characters and a NUL.
- `edit_add_dec` of 0 is `0`. Width 4 of 12 is two spaces then `12`. Width 4 of 12345 is `12345`.
- `edit_add_field` of `HI` at width 16 is `HI` plus 14 spaces. A 16-character name is copied with no extra space. `edit_add_n` of `ABCDEFGHIJ` with n 8 is `ABCDEFGH`. `edit_add_n` of `AB` with n 8 is `AB`.
- The startup sentence is `REU: 16320KB  MAX 4347 PAGES`.

The cascade, the cursor, and the save are checked on the C64 Ultimate that `tools/c64u.py` drives. The script is `tools/test_page_insert_c64u.py`. It uses that file's reset, program load, keyboard buffer, and `$0400` read. It runs the editor in 40-column mode. It does not run `tools/test_w64conv_c64u.py`.

`boot_editor` resets the machine. After each reset, `c64u drives list --json` is read. Drive 8 is the entry whose `bus_id` is 8. When its image already names `W64.d64`, it is left mounted. When drive 8 has no image, the script mounts `/SD/My Files/W64.d64` on the drive letter of that entry. When drive 8 has a different image, the script stops. A missing drive 8 stops the script. The disk is still mounted when the script finishes.

The source file is `DICEROLL-SRC`. The script does not save over it. The save test writes `DICEINS` and leaves that file on the disk. An existing `DICEINS` is overwritten by answering `Y`.

A wrapped editor line occupies more than one screen row. The script does not compare raw screen rows. CTRL+G puts the cursor at column 0 of a title line, on the first screen row of that editor line. The bytes compared are screen columns 3 through 39 of that cursor row. A blank row is spaces there. The reverse-video bit on the cursor's own cell is ignored. The gutter digits are columns 0 and 1. Goto 1 is title line 1. Title line 48 is gutter `47` of the first page. Title line 49 is gutter `00` of the second page.

Each case loads `DICEROLL-SRC` again. 79 Right keys from column 0 land on the last character column of that editor line and stay there on the last row of a page.

1. Read title lines 1, 48, and 49. Call those texts A, B, and C. Goto 1 and Return. Title line 1 is blank. Title line 2 is A. Title line 49 is B. Title line 50 is C. F2 saves `DICEINS`. Loading `DICEINS` shows title line 1 blank and title line 2 as A.
2. Read title lines 48 and 49. Call those texts A and B. Goto 48, send 79 Right keys, and Return. Title line 48 is still A. The cursor's title line is 49 and its text columns are blank. Title line 50 is B.
3. CTRL+N until the title page stops changing, then Down until another Down leaves the gutter digits unchanged. Read those two digits. When they are below `47`, Up until the digits are `00`, then Return. The title page count is unchanged. When the digits are `47`, remember that row's text columns and the title line number, send 79 Right keys, and Return. The title page count is one greater. The cursor is on a blank row whose gutter is `00`. Goto of the remembered title line still shows the remembered text.

A VICE run is not this check. 80-column mode uses the same `new_line` and is not read back, because the driver only sees `$0400`.

## Docs

`AGENTS.md` replaces the sentence that says `new_line` opens a page only from the last row and otherwise shows `PAGE FULL`. The new text says Return carries the last row of a full page to row 0 of the next page, a full page carries onward, a new page is added only for a row that falls off the end, a last-row Return follows the tail, an earlier row stays put, `REU FULL` refuses when the REU has no free page, a spill clears undo, and Return on a short page saves undo before the cut while a paste that inserts a row clears that snapshot. It also says `num_pages` is the stored page count and is not rebuilt from `total_lines`.

`README.md` adds one sentence to the multi-page feature: Return on a full page pushes the last line onto the next page.

`docs/Todo-List.md` checks off the page-full item after the Ultimate script passes, with a short note that the last row is carried forward and that `DICEROLL-SRC` showed it. Help text is left as it is.
