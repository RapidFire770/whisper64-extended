# Tool question order, line progress, and LF breaks

`W64CONV`, `W64RENUM`, and `W64LABEL` ask their questions from the title downward, paint `LINE n` while a job walks the file, and treat LF as a line break on every SEQ read. `W64CONV` menu 1 asks whether to renumber, and it asks for a step only when the answer is yes. The editor is unchanged. No new source file is added.

This replaces the fixed prompt rows in `docs/superpowers/specs/2026-11-13-rem-step-prg-renumber-design.md`. Strip, step parsing, PRG renumber, and the tail copy stay as that spec describes them.

## Screen

Row 0 stays the centered program name. Row 1 stays the centered `REU:` line. The menu, the help pages, and `REU 128KB REQUIRED` on row 8 stay where they are.

A job uses one next-row counter in `src/conv/toolui.c`. It starts at 3 after the menu choice. `tool_take_row` returns the current row and adds one. The read drive takes row 3 and the write drive takes row 4. The counter is then set to 6, so row 5 is never taken. The filename takes row 6, and `tool_prepare` returns with the counter at 7. A question the program does not ask leaves no gap.

| Row | Contents |
|---|---|
| 3 | `READ DRIVE (8-15):` |
| 4 | `WRITE DRIVE (8-15):` |
| 5 | Blank after both drives are accepted. `USE 8-15` is red here until the next answer, then the row is blanked. |
| 6–23 | The file list. Eighteen names, the selected row yellow. |
| 6 onward, after the list | Filename, the program's own questions, then the job. |

`draw_title` still clears the screen for the list, then paints the title and the REU line. The drive answers are not on screen during the list. After a file is chosen, `tool_prepare` paints row 3 and row 4 again with the accepted drive numbers, blanks rows 6–23, and asks `OUTPUT FILENAME:` on row 6. An empty name still copies the input name, and that name is painted on row 6. A rejected name paints `BAD NAME` in red on the next row. Stop on the list still returns to the menu without `ANOTHER`. `NO FILES` is red on row 6, waits for a key, and returns to the menu.

Each later question takes the next row:

- `W64CONV` menu 1 asks `STRIP REM? Y/N`, then `RENUMBER? Y/N`. `Y` and `y` mean yes. `N` and `n` mean no. Any other key waits. Stop cancels the way `tool_ask_yn` already does. `STEP: ` is the next row only when renumber is yes.
- `W64LABEL` asks `STRIP REM? Y/N`, then `STEP: `.
- `W64RENUM` asks `STEP: `.
- `W64CONV` menu 2 asks nothing else.
- `OVERWRITE? Y/N` is the next row only when the output name is already on the write drive. `N` paints `CANCELLED` in green on that same row. It does not take another row.
- `LINE n` is the next row. Later progress repaints that same row.
- The result is the next row: `WROTE`, `RENUMBERED`, `SEQ n TRUNC t ML m`, or the red error from `tool_finish`. `LINE n` stays visible above it.
- `ANOTHER? Y/N` is the next row.

A full `W64CONV` menu 1 job that renumbers and overwrites ends at row 13. The other jobs are shorter.

`W64CONV` help row 6 changes from `STEP EMPTY KEEPS NUMBERS` to `1. N KEEPS NUMBERS`. The blank help row 16 becomes `1. Y ASKS STEP, EMPTY IS 10`. The error glossary does not move. `W64LABEL` and `W64RENUM` keep `STEP EMPTY IS 10`.

## Renumber prompt

`tool_parse_step` is unchanged. An empty field is 10. A non-digit or a value past 63999 is `NUM BIG` from `tool_parse_step`. A typed 0 is passed through and the renumber returns `NUM BIG`. Nothing is written.

`W64CONV` menu 1 calls `seq_to_prg` with `keep` set when the renumber answer is no. That is the current empty-step path: `seq_store` tokenizes the numbers already in the SEQ. The step field is not read, and `tool_parse_step` is not called. Yes passes `keep` clear and the parsed step, including 10 when the field was empty. Menu 2 is unchanged.

`W64LABEL` and `W64RENUM` still pass every answer through `tool_parse_step`. An empty step remains 10.

## Line breaks

The editor splits a SEQ on CR or on LF, and a CR followed by an LF is an empty line. `tool_read_seq_line` already does that split. `W64CONV` menu 1 uses it for both the keep path and the renumber path. The Strip Y readers in `W64LABEL` use it too.

`read_seq_reu` in `src/conv/label_main.c` and `src/conv/renum_main.c` copies bytes unchanged, and `labels_to_numbered_store` and `renumber_seq_store` split on CR only. A Strip N run of a file whose later labels are separated by LF never sees those labels. `diceroll-src.seq` is that file: `LD:` is past an LF, and the job stops with `NO LABEL` at `GOSUB LD`.

`conv_seq_byte` lives in `src/conv/token.c` and is declared in `include/w64tools.h`. A byte of 10 becomes 13. Every other value is returned as it arrived. Both `read_seq_reu` functions store `conv_seq_byte` of each data byte. The scanners keep splitting on CR. A CR followed by an LF is stored as two CRs, the same empty line the editor shows. Output files are still CR.

## Progress

`conv_set_progress(void (*fn)(unsigned int row))` is defined in `src/conv/renumber.c` and declared in `include/w64tools.h`. The pointer starts null. The host tests leave it null. Each tool sets it, before the job, to a function that paints `LINE ` and the number on the progress row taken above. The store walks call it once per source line. A null pointer paints nothing.

The number restarts at 1 on each full walk. For a label job those walks are the read, the label collection, both emit passes, and `tokenize_store`. For a SEQ renumber they are the read, the map build, both rewrite passes, and `tokenize_store` when a PRG is written. A restart means the next walk started. It is not a new bug.

Disk reads paint the same `LINE n` text and then `cbm_k_chkin(2)`. `tool_note_progress` clears the KERNAL channel, and the read has to claim logical file 2 again. The store walks have no open channel, so their callback does not call `chkin`.

On a SEQ, `n` is the 1-based source row, the same number `LINE LONG` and `NO LABEL` append. A line `tool_read_seq_line` rejects before the counter increments reports `seen + 1`. On a PRG, `n` is the BASIC line number already used by `LINE LONG`. The in-progress `PRG n` and `SEQ n` counters go away. `WROTE`, `RENUMBERED`, and `SEQ n TRUNC t ML m` stay, on the result row.

## The 45600 report

Host numbering of the CR form of `diceroll-src.seq` at step 1 keeps the joystick line as text: the line contains `J% = PEEK(56320)` and its number is the ordinary step-1 number. 45600 is the 16-bit value of a space byte followed by the BASIC token for `=`. This work does not change numbering to chase that value.

The Ultimate check loads the program `W64LABEL` wrote and lists the joystick line. A normal step-1 number and the text `J% = PEEK(56320)` close the report. A listing that shows 45600, with the source line still `J% = PEEK(56320)`, stops the work for a separate fix.

## Paths

The mount string `USB1/MY FILES/W64.d64` becomes `/SD/My Files/W64.d64` in:

- `tools/test_page_insert_c64u.py`
- `docs/superpowers/specs/2026-11-14-page-spill-design.md`
- `docs/superpowers/plans/2026-11-14-page-spill.md`

Drive 8 is left mounted when its image already names `W64.d64`. An empty drive 8 mounts `/SD/My Files/W64.d64`. A different image still stops the script.

## Tests

From the repo root:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c
gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c
gcc -Wall -Wextra -Iinclude -o /tmp/test_bigfile tools/test_bigfile.c src/conv/renumber.c src/conv/token.c
python3 tools/test_basic_prg.py
```

`tools/test_renumber.c` gains two checks. `conv_seq_byte` on the bytes `A\r\nB` produces `A\r\rB`. The second fixture is the bytes `J% = PEEK(56320)\nGOSUB LD\nLD:\nPRINT "HI"`, passed through `conv_seq_byte`, then `labels_to_numbered` at step 1. The joystick line still contains `J% = PEEK(56320)`. `GOSUB LD` uses the number of the `PRINT "HI"` line, because a label alone shares the next code line. The existing renumber, big-file, format-line, and `test_basic_prg.py` checks stay.

The three tool PRGs are built with the repo's CMake command. The maps for `w64conv`, `w64renum`, and `w64label` have `__heap_start` at or below `$C800` and `__stack` at `$D000`.

On the C64 Ultimate, with `/SD/My Files/W64.d64` mounted as above, `W64LABEL` converts the dice SEQ. `LINE n` changes while the job runs. The joystick line in the written program keeps `J% = PEEK(56320)` and a step-1 number. `tools/test_w64conv_c64u.py` resets the machine and is not part of this work. If the Ultimate is unreachable, stop and ask. A VICE run is not a substitute.

## Docs

`AGENTS.md` records the next-row counter, the `RENUMBER?` prompt, `conv_seq_byte`, and `LINE n` on the progress row. The heap lines are updated from the new maps when they move.

`README.md` replaces the sentence that says `W64CONV` menu 1 always asks `STEP:` and that Return keeps the SEQ numbers. Menu 1 asks `RENUMBER? Y/N`. No keeps the numbers. Yes asks `STEP:`, and an empty step is 10. The same section says a long job paints `LINE n`, and that all three tools treat LF as a line break.

`docs/Todo-List.md` checks off the interface-order item, the silent dice-file item, the 45600 item, and the `W64CONV` renumber-prompt item. The dice note names `/SD/My Files/W64.d64`. The 45600 note says the host run kept `J% = PEEK(56320)`, and it records what the Ultimate listing showed.
