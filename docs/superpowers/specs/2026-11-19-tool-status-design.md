# Conversion status text and a clear drive screen

`W64CONV`, `W64RENUM`, and `W64LABEL` name the stage on the progress row, and they clear the menu before asking for the read and write drives. The editor is unchanged. No new source file is added.

This replaces the `LINE n` progress rule in `docs/superpowers/specs/2026-11-15-tool-flow-design.md`. Row assignment, question order, LF breaks, strip, step parsing, and the result lines stay as that spec describes them.

## Progress row

The progress text uses the same row today's `LINE n` uses. Each stage repaints that row. The result (`WROTE`, `RENUMBERED`, `SEQ n TRUNC t ML m`, or a red error) still takes the next row. `LINE LONG` and `NO LABEL` stay error text. They are not progress text.

`paint_row` already blanks the row before drawing, so a shorter stage does not leave the tail of a longer one. `tool_note_progress` builds the text in a 40-byte stack buffer. The longest text is `NUMBERING LABELED LINES: 65535` (30 characters plus a NUL).

`tool_note_progress` still calls `cbm_k_clrch`. A disk read that paints progress still calls `cbm_k_chkin(2)` afterwards, at the call site that already does. The store walks have no open channel, so their callback does not call `chkin`.

### What each stage paints

The label includes the colon and the space. The number is appended.

| Stage | Text | Number |
|---|---|---|
| SEQ read, REM kept | `READING LINE: 23` | 1-based source row |
| SEQ read, REM stripped | `STRIPPING LINE: 23` | 1-based source row |
| PRG read | `PRG LINE: 100` | BASIC line number |
| First store walk | `PASS 1 LINE: 23` | 1-based source row |
| Measure walk | `PASS 2 LINE: 23` | 1-based source row |
| Renumber write | `RENUMBERING LINE: 10` | 1-based source row |
| Label write | `NUMBERING LABELED LINES: 130` | 1-based source row |
| Tokenize | `TOKENIZING LINE: 23` | 1-based source row |

A count still restarts at 1 when the next walk starts. `PRG LINE:` does not. It shows the BASIC line number already used by `LINE LONG` on that path.

`seq_store` tokenizes while it reads, including when strip is on. That single walk paints `TOKENIZING LINE:`. It does not also paint `STRIPPING` or `READING`.

### Who paints it

`conv_set_progress(void (*fn)(const char *label, unsigned int row))` is defined in `src/conv/renumber.c` and declared in `include/w64tools.h`. The pointer starts null. A null pointer paints nothing. Each tool sets it to `tool_progress_line` before the menu.

`tool_progress_line(const char *label, unsigned int n)` paints that label and number on the progress row. `renumber_seq` and `labels_to_numbered` do not call it. The RAM tests leave the pointer null.

The string literals live next to the walk that emits them. `--gc-sections` drops a walk the program does not call, and that walk's text with it. There is no shared table of every phrase.

Disk reads call `tool_progress_line` themselves:

| Call | Text |
|---|---|
| `read_seq_reu` in `src/conv/renum_main.c` and `src/conv/label_main.c` | `READING LINE:` |
| `read_seq_stripped` in `src/conv/label_main.c` | `STRIPPING LINE:` |
| `stash_seq_lines` in `src/conv/conv_main.c`, strip off | `READING LINE:` |
| `stash_seq_lines`, strip on | `STRIPPING LINE:` |
| `seq_store` in `src/conv/conv_main.c` | `TOKENIZING LINE:` |
| `prg_store` in `src/conv/conv_main.c` | `PRG LINE:` |
| `prg_to_text` in `src/conv/renum_main.c` | `PRG LINE:` |

Store walks call the progress function from `note_row`:

| Walk | Text |
|---|---|
| `renumber_seq_store` map build | `PASS 1 LINE:` |
| `renumber_seq_store` pass 0 | `PASS 2 LINE:` |
| `renumber_seq_store` pass 1 | `RENUMBERING LINE:` |
| `labels_to_numbered_store` collection | `PASS 1 LINE:` |
| `labels_to_numbered_store` pass 0 | `PASS 2 LINE:` |
| `labels_to_numbered_store` pass 1 | `NUMBERING LABELED LINES:` |
| `tokenize_store` | `TOKENIZING LINE:` |

A job the user can see, in order:

- `W64CONV` menu 1, numbers kept: `TOKENIZING LINE:`
- `W64CONV` menu 1, renumber, strip off: `READING`, `PASS 1`, `PASS 2`, `RENUMBERING`, `TOKENIZING`
- `W64CONV` menu 1, renumber, strip on: `STRIPPING`, then the same four store walks
- `W64CONV` menu 2: `PRG LINE:`
- `W64RENUM` menu 1: `READING`, `PASS 1`, `PASS 2`, `RENUMBERING`
- `W64RENUM` menu 2: `PRG LINE:`, `PASS 1`, `PASS 2`, `RENUMBERING`, `TOKENIZING`
- `W64LABEL`, strip off: `READING`, `PASS 1`, `PASS 2`, `NUMBERING LABELED LINES`, `TOKENIZING`
- `W64LABEL`, strip on: `STRIPPING`, then the same four store walks

## Drive screen

`tool_prepare` calls `draw_title` before the first drive question, while the menu title is still on row 0. `draw_title` clears the screen, puts those 8 title bytes back, and paints the REU line. The menu items are gone before `READ DRIVE (8-15):` is drawn.

Rows do not move. The read drive is row 3. The write drive is row 4. `USE 8-15` is still red on row 5 until the next answer, then that row is blanked. An empty entry is still 8. Stop still sets `CONV_CANCEL` and returns. `ANOTHER? Y/N` is then painted on the next free row of that cleared screen.

The file list still calls `draw_title` again. The drive numbers are painted on rows 3 and 4 after a file is chosen, as they are now. Help, the main menu, and `REU 128KB REQUIRED` are unchanged. Those already own the whole screen.

## Tests

From the repo root:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c
gcc -Wall -Wextra -Iinclude -o /tmp/test_bigfile tools/test_bigfile.c src/conv/renumber.c src/conv/token.c
gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c
python3 tools/test_basic_prg.py
```

`tools/test_renumber.c` records the label with the row. A null progress pointer still paints nothing, and the existing renumber fixtures stay green.

`labels_to_numbered_store` on `10 END\r20 END` reports six calls: `PASS 1 LINE:` for rows 1 and 2, `PASS 2 LINE:` for rows 1 and 2, `NUMBERING LABELED LINES:` for rows 1 and 2.

`renumber_seq_store` on the same text reports six calls: `PASS 1 LINE:`, `PASS 2 LINE:`, and `RENUMBERING LINE:`, each for rows 1 and 2.

`tokenize_store` on that same stored SEQ reports `TOKENIZING LINE:` for rows 1 and 2.

The three tool PRGs are built with the repo's CMake command. The maps for `w64conv`, `w64renum`, and `w64label` have `__heap_start` at or below `$C800` and `__stack` at `$D000`.

On the C64 Ultimate, one tool is run far enough to see the drive questions on a cleared screen, title and REU line still present, and far enough to see one named stage replace `LINE`. `tools/test_w64conv_c64u.py` resets the machine and is not part of this work. If the Ultimate is unreachable, stop and ask. A VICE run is not a substitute.

## Docs

`AGENTS.md` names the stage text and says `tool_prepare` clears the screen before the drive questions. The heap lines are updated from the new maps when they move.

`README.md` replaces the sentence that says a long job paints `LINE n`. It names the stage text, and it says the drive questions are asked on a cleared screen.

`docs/Todo-List.md` checks off the line-status item and the drive-screen item.
