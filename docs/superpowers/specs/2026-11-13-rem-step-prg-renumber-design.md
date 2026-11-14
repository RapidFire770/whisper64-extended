# REM strip, line step, and PRG renumber

`W64CONV` and `W64LABEL` can drop `REM` comments while converting a SEQ to a PRG, and both can take a line-number step. `W64RENUM` can renumber a PRG as well as a SEQ. The PRG job copies the bytes that follow the BASIC program, aside from the synthetic CR `tool_trailing_ml` already ignores. The editor is unchanged. No new source file is added.

## Prompts

`tool_prepare` still asks for the drives, the file, and the output name, then blanks rows 2–19. Row 20 stays `OUTPUT FILENAME`. Row 18 and row 21 are free for the new questions. `OVERWRITE? Y/N` still runs inside the job, on row 17, and only when the output name is already on the write drive.

**W64CONV.** The menu stays `1 SEQ TO PRG` and `2 PRG TO SEQ`. Mode 2 is unchanged, including help row 19, `ML 1: EXTRA BYTES NOT COPIED`. Mode 1 asks:

- Row 18: `STRIP REM? Y/N`. `Y` and `y` strip. `N` and `n` leave the comments. Any other key waits. Stop sets `verify_code` the way `tool_read_field` does, and the caller takes the same `tool_another` path a cancelled field already takes.
- Row 21: `STEP: `, the same 8-byte field `W64RENUM` uses. Return on an empty field keeps the line numbers already in the SEQ. A typed value goes through `tool_parse_step`.

Help row 6 is `STEP EMPTY KEEPS NUMBERS`. Help row 20 is `Y STRIPS REM AND KEEPS THE LINE`. The error and status rows stay where they are.

**W64LABEL.** The same two questions, on the same rows, with the same keys. Return on an empty step means 10. Help row 4 changes from `STEP IS 10` to `STEP EMPTY IS 10`. Help row 6 is `Y STRIPS REM AND KEEPS THE LINE`.

**W64RENUM.** The menu is:

- Row 3: `1 RENUMBER SEQ`
- Row 4: `2 RENUMBER PRG`
- Row 5: `? HELP`
- Row 6: `RUN/STOP EXITS THE PROGRAM`

Choice 1 is the current SEQ job: `tool_prepare('S')`, `STEP:` on row 21, write a SEQ. Choice 2 is `tool_prepare('P')`, the same step prompt, write a PRG. Neither choice asks about `REM`. Empty step still means 10.

Help:

- Row 3: `1. SEQ - RENUMBERS LINES IN SEQ`
- Row 4: `2. PRG - RENUMBERS A PRG, KEEPS ML`
- Row 5: `STEP EMPTY IS 10`
- Row 6: `?  HELP - SHOWS THIS HELP SCREEN`

The error block stays on rows 7–14.

## REM strip

`conv_strip_rem(char *line)` is declared in `include/w64tools.h` and defined in `src/conv/token.c`. The line is NUL-terminated, at most 79 characters, the buffer `tool_read_seq_line` already fills. The function rewrites that buffer in place.

The scan matches `conv_tokenize`. A quote toggles string mode. `DATA`, recognized by `conv_keyword_len`, copies bytes until a colon outside quotes, and that colon starts the next statement. A quote check runs before the `DATA` check, so a colon inside a quoted `DATA` item stays data. Outside quotes and `DATA`, `conv_keyword_len` at the current byte recognizes `REM`. `REMEMBER` stays. Lowercase `rem` stays. `REM` inside quotes or `DATA` stays.

The match consumes `REM` and the rest of the line. The colon that introduced it comes off too, with the spaces around that colon. The colon stays when the text that remains is a label and that colon is the label's colon: optional spaces, a letter, then letters or digits, then the colon at the end, and the name is not an exact entry from the token table in `src/conv/token.c`. A name longer than 11 still keeps that colon. The label pass reports the long name. Any other trailing colon is a statement separator and comes off.

The later pass sees what remains:

| Source line | After the strip |
|---|---|
| `10 PRINT "HI": REM NOTE` | `10 PRINT "HI"` |
| `10 PRINT "HI":REM NOTE` | `10 PRINT "HI"` |
| `10 REM NOTE` | `10` |
| `10 REM:PRINT "HI"` | `10` |
| `start: REM menu` | `start:` |
| `start:PRINT "HI":REM bye` | `start:PRINT "HI"` |
| `10 PRINT "REM"` | `10 PRINT "REM"` |
| `10 DATA REM` | `10 DATA REM` |
| `10 DATA "HI":REM NOTE` | `10 DATA "HI"` |
| `10 REMEMBER` | `10 REMEMBER` |
| `10 rem note` | `10 rem note` |
| `10 IF A THEN REM NOTE` | `10 IF A THEN` |
| `REM NOTE` | empty |

`10` tokenizes as a numbered line with no statement, so `GOTO 10` still lands. `start:` follows the label rule already in `labels_to_numbered`: it shares the next code line's number, or it gets its own empty numbered line when a blank line or the end of the file follows. An empty result on the stream path is the blank line `conv_tokenize` already skips. On a REU path the empty result is stored as one CR, which is a blank line. `tokenize_store` skips that line. In `W64LABEL` that blank line is the existing separator that gives each held label its own empty numbered line.

`10 IF A THEN REM NOTE` becoming `10 IF A THEN` is a successful conversion. BASIC reports a syntax error if that line runs.

The strip runs before numbering and before tokenization. `W64CONV` with an empty step strips the line `tool_read_seq_line` just read, then calls `conv_tokenize`. `W64CONV` with a typed step, and `W64LABEL` when the answer is Y, split the SEQ on CR or LF through `tool_read_seq_line`, strip, and store each line plus a CR. `W64LABEL` when the answer is N keeps the raw byte copy it has today.

## Line step

Return on an empty `STEP:` field is the only "keep the numbers" answer. A typed 0 is passed to the renumber and comes back `NUM BIG`. A non-digit or a value past 63999 is `NUM BIG` from `tool_parse_step`. Nothing is written.

**W64LABEL.** `labels_to_numbered_store` receives the parsed step instead of the fixed 10. Empty still parses as 10.

**W64CONV, empty step.** `seq_store` stays the stream it is. The disk line is read, stripped when asked, then tokenized. A line that stripped down to its number is stored as a BASIC line with no statement bytes.

**W64CONV, typed step.** The SEQ is stored at REU address 0. Each line is read on CR or LF, stripped when asked, and stored with a CR. Row 10 counts those lines with `tool_note_progress(10, "SEQ ", n)`. `renumber_seq_store` rewrites the lines and the targets of `GOTO`, `GOSUB`, `THEN`, `ELSE`, `GO TO`, and `GO SUB`. `tokenize_store` reads that numbered text and writes the PRG above it. The renumber map is already applied, so the ceiling is `reu_bytes` and the PRG may reuse the map's bytes.

A failure uses `tool_show_err`: `NO NUM`, `NUM BIG`, `LINE LONG` with the source row, `TOO BIG`, or `PRG TOO BIG`. The empty-step stream still reports `LINE LONG` with no row. A failure writes nothing. The renumber and tokenize passes stay as quiet as `W64LABEL` and `W64RENUM` are today.

`w64conv.prg` links `src/conv/renumber.c` so it can call `renumber_seq_store`. `--gc-sections` drops the RAM entry points that the host tests call. `__heap_start` stays at or below `$C800`. `__stack` stays `$D000`.

## PRG renumber

Choice 2 reads a PRG, detokenizes it into the REU, renumbers that text, tokenizes it, and writes a PRG. The overwrite probe is `name,P,R`. The write is `name,P,W`, or `@0:name,P,W` when the name is already there. The status line is `RENUMBERED`.

The read uses the checks from `W64CONV` mode 2. The first two bytes must be `01 08`, or the job stops with `NOT BASIC PRG`. A broken link stops with `BAD BASIC PRG`. A line number past 63999 stops with `NUM BIG`. Each BASIC line goes through `conv_format_line`. A negative length or a set truncated flag stops with `LINE LONG` and that BASIC line number. The shortened line mode 2 may write is not used. Nothing is written on failure.

Text is stored at REU address 0, one CR after each detokenized line. Row 10 counts BASIC lines with `tool_note_progress(10, "PRG ", n)`. After the zero link, the remaining bytes are the tail. The CHRIN byte that arrives with STATUS bit 6 is kept. A following synthetic CR is not part of the tail, the same rule as `tool_trailing_ml`. Those bytes are written at `text_len`. When `text_len + tail_len` is greater than `reu_bytes`, the job stops with `TOO BIG`. Otherwise the tail is moved to `reu_bytes - tail_len`, copying from the high end so an overlap stays intact. A zero-length tail is not moved.

`renumber_seq_store` and `tokenize_store` both receive `reu_bytes - tail_len` as the ceiling. The map and the new PRG stay below the tail. A renumber or tokenize failure uses `tool_show_err`, so `LINE LONG` from those passes carries the 1-based row in the detokenized text. The `LINE LONG` from `conv_format_line` carries the BASIC line number instead. On success the tail is copied to the byte after the new PRG, from the low end, because that destination is below the source. `tool_flush_at` writes one span: the load address, the BASIC program, the zero link, and the tail. An empty BASIC program is still the four bytes `01 08 00 00`, with the tail after them.

`w64renum.prg` links `src/conv/token.c`. `__heap_start` stays at or below `$C800`. `__stack` stays `$D000`. The same bound applies to `w64label.prg` after the prompt and reader change.

## Tests

From the repo root:

```bash
gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c
gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c
gcc -Wall -Wextra -Iinclude -o /tmp/test_bigfile tools/test_bigfile.c src/conv/renumber.c src/conv/token.c
```

`tools/test_format_line.c` calls `conv_strip_rem` on each row of the table above. `tools/test_renumber.c` renumbers `10\r20 GOTO 10\r` at step 5 and expects `5\r10 GOTO 5\r`. It also strips each of `start: REM menu`, `PRINT "HI"`, and `GOTO start`, joins them as `start:\rPRINT "HI"\rGOTO start`, and `labels_to_numbered` at step 10 expects `10 PRINT "HI"\r20 GOTO 10`. `conv_strip_rem` sees one line, so the test does not pass it the CR-joined buffer. The existing format, renumber, and big-file checks stay. `tools/test_format_line.c` already covers a line `conv_format_line` can shorten and a line it refuses. The PRG reader treats either result as `LINE LONG`.

The three tool PRGs are built with the repo's CMake command. The maps for `w64conv`, `w64renum`, and `w64label` have `__heap_start` at or below `$C800` and `__stack` at `$D000`. Editor sources are not edited.

`tools/test_w64conv_c64u.py` is the disk round trip. It resets the Ultimate, so it is not part of this work unless Josh asks for it.

## Docs

The implementation updates `AGENTS.md` for the step prompt, the REM strip, the PRG job, and the heap addresses from the new maps. It checks off the three open items under `W64TOOLS` in `docs/Todo-List.md`.

`README.md` gets one pass in the same change. It describes the three tools as they work after this feature, and it fills the editor facts that are already true and missing from that file. `src/help.c` stays as it is. Help page 2 names the tools in short strings so the editor link stays under `$CC00`. The README carries the longer description.

Replace the paragraph that starts `` `W64CONV` menu `1` is SEQ to PRG `` with:

`W64CONV` menu `1` reads a numbered SEQ and writes a PRG. It asks `STRIP REM? Y/N`. `Y` removes a real `REM` and the rest of that line. A `REM` inside quotes stays, and so does `REMEMBER`. A line that is only a REM keeps its line number. It then asks `STEP:`. Return keeps the numbers in the SEQ. A number from 1 to 63999 renumbers and rewrites `GOTO`, `GOSUB`, `THEN`, `ELSE`, `GO TO`, and `GO SUB`. Menu `2` reads a PRG and writes a SEQ. A long line is cut back to the last whole statement, and the status line counts those cuts as `TRUNC`. Bytes after the BASIC program are not copied (`ML 1`).

`W64RENUM` menu `1` renumbers a SEQ and writes a SEQ. Menu `2` renumbers a PRG and writes a PRG. Menu `2` copies the bytes that follow the BASIC program. Both ask `STEP:`. An empty step is 10.

`W64LABEL` reads a labeled SEQ and writes a PRG. It asks `STRIP REM? Y/N` with the same rules. `start: REM menu` keeps the label. It asks `STEP:`. An empty step is 10.

Keep the sentences that follow that paragraph: drive 8–15 with an empty entry of 8, F2 stays SEQ, and N or Stop at the another-file prompt leaves the tool. Add two sentences there. `?` opens help, and Stop leaves that page. RUN/STOP on the main menu exits.

The key table gains `CTRL+N` (next page) and `CTRL+B` (previous page). The Goto row says the title-bar line is the file-wide row, 1-based, and a page is 48 lines. The features list says a line longer than the screen continues on the next row, and Return is what starts a new editor line. The BASIC Mode section says the 80th character is not stored and the status row says `BASIC LINE FULL`. F8 is two pages: Right shows page 2, Left shows page 1, and Stop or Return leaves.

Add a short `bas2seq` subsection after the three tools. It is the desktop program in `tools/bas2seq`. From `tools/`, `python3 -m bas2seq` reads a PC `.bas` file and writes a SEQ the editor can open. It does not number lines. `W64LABEL` still does that on the C64.

## Out of scope

`W64RENUM` does not ask about `REM`. Mode 2 of `W64CONV` still drops a machine-language tail and still shortens a long line at a colon. The "large job looks frozen" item stays open. `tools/bas2seq` behavior is unchanged. The README only gains the subsection above. `src/help.c` is not edited.
