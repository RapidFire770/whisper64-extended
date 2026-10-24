# AGENTS.md — Coding Agent Guidelines for Whisper64-Extended

**Project:** Whisper64-Extended — a text editor for the Commodore 64, aimed at real hardware and especially the C64 Ultimate. Forked from https://github.com/sandlbn/whisper64. This repo is https://github.com/RapidFire770/whisper64-extended.
**Primary language:** C, cross-compiled with the LLVM-MOS SDK (`c64` platform). No Rust, no extra crates.
**Status:** Editing, paging, SEQ load/save, two-color BASIC highlighting in 40- and 80-column mode, display wrap, file-wide goto, and page keys. F4 only toggles BASIC color. SEQ/PRG conversion is `W64CONV`, full-file renumber is `W64RENUM`, and labels are `W64LABEL`. The editor does not link them. 80-column keyword color is one foreground color per hires cell (two glyphs share it). Mouse and joystick support was removed so the editor stays under `$D000`. Open work is `docs/Todo-List.md`.
**Last Updated:** October 24, 2026

This file defines how coding agents should work on the project. It exists to keep changes consistent. AGENTS.md takes priority over any older design notes. The sibling project `~/Documents/Programming/C64/BASIC-IDE` (Kick Assembler) is a separate, partly working attempt. Do not copy it in. Use it only as a record of tokenizer pitfalls.

**IMPORTANT:**

- Always check available skills and invoke them when relevant to the user's request.
- Delete dead or obsolete code, unreachable code, or functions that are never called.
- Code must look hand-written for this repository. Changes that read like generic AI-generated code will be rejected automatically: unnecessary helper layers, vague names, boilerplate comments, defensive branches without a real failure mode, broad rewrites, or code that ignores the local style.
- For small one file changes, they can be performed on the main git branch.
- For medium to large multi file changes and refactors use writing-plans, executing-plans and subagent-driven-development skills.
- Always use a Code reviewer subagent to review the work afterward. Create a commit once work has been validated.
- Verify that any changes do not break existing functionality.
- Keep this AGENTS.md file updated with information about the project that other AI agents would need to know to keep the project consistent from session to session. (Project layout/structure, technology stack, tool chain, coding standards & style, testing, pitfalls and conflicts to avoid, features, etc.)
- When a bug is fixed or feature is implemented check it off the user's `docs/Todo-List.md` and include it in the commit for the final work.
- Real C64U hardware is available for testing at http://192.168.100.193 via REST API. Use the installed c64u command line program and its agent skills or the c64u.py driver tool the projects tools folder (this tool can be modified if needed). This hardware takes priority for testing over VICE. The C64U is running with JiffyDOS ROMS for the system and its disk drives. If the C64U is unreachable for testing, pause and ask the user to bring it back online.

---

## Toolchain

LLVM-MOS SDK on this machine:

```text
/home/josh/Documents/Programming/C64/tools/llvm-mos
```

That path is also in `docs/llvm-mos-sdk location.md`, which is gitignored. The README's `~/llvm-mos` prefix is the upstream example, not this machine.

Cloud Agents install LLVM-MOS SDK v23.3.0 at `/opt/llvm-mos`. `mos-c64-clang` and `llvm-ar` are linked in `/usr/local/bin`. The SDK's `clang` is not on `PATH`, so it does not replace `/usr/bin/clang`. `cmake -S . -B build` finds `llvm-mos-sdk` without `CMAKE_PREFIX_PATH`. `x64sc` and `c1541` come from the `vice` package. Ubuntu does not ship Commodore ROMs. The image uses MEGA65 Open ROMs (LGPL-3.0, commit `5192c683a098204e835b8c5529ecd24c808eba92`) as `/usr/share/vice/C64/kernal-901227-03.bin`, `basic-901226-01.bin`, and `chargen-901225-01.bin`. Those files are not Commodore's ROMs. Drive ROMs are not installed, so hardware-level 1541 emulation is unavailable. `./run_vice.sh` still injects `whisper64.prg`.

```bash
cd build
cmake -DCMAKE_PREFIX_PATH=/home/josh/Documents/Programming/C64/tools/llvm-mos ..
make
```

`CMakeLists.txt` sets `LLVM_MOS_PLATFORM` to `c64` before `project()`. Output is `build/whisper64.prg`, `build/w64conv.prg`, `build/w64renum.prg`, and `build/w64label.prg`. The editor is `-Os`, `-ffunction-sections`, `--gc-sections`. The three tools are `-Oz` with the same section flags. A MinSizeRel cache still passes `-Os` first; the target `-Oz` comes after it and wins. Do not compile the three at `-Os`. Each links `-lexit-return`. N at the another-file prompt does not return through it. The editor does not link it, and the editor has no exit. The October 24, 2026 maps put `__heap_start` at `$4B2C` (`w64conv`), `$622C` (`w64renum`), and `$907D` (`w64label`), with `__stack` at `$D000` on each. A 512KB `whisper64.reu` image and `whisper64.d64` are created by the build and by `run_vice.sh`. Both are gitignored, as is `build/`.

Run in VICE:

```bash
./run_vice.sh
```

That starts `x64sc` with REU enabled, the REU image writable, and the D64 on drive 8. On the C64 Ultimate, `LOAD "WHISPER64",8,1` then `RUN`. `tools/c64u.py` drives the Ultimate's REST API. The host is hardcoded to `http://192.168.100.193`. It injects keys through the KERNAL keyboard buffer at `$0277` / `$C6` and reads the 40-column screen at `$0400`. It cannot see 80-column bitmap mode.

There is no unit-test target. Do not claim a C64 behavior works unless VICE or the Ultimate showed it. A host Python script is the right place for a tokenizer round-trip test. Do not add a crate for that.

## Layout

```text
include/     headers, one per module, plus font_4x8.h, whisper64.h, w64tools.h, w64toolui.h, w64reu.h
src/         editor .c files
src/conv/    W64CONV, W64RENUM, and W64LABEL
tools/       c64u.py (Ultimate REST driver), test_basic_prg.py, test_renumber.c, test_bigfile.c, test_format_line.c, test_w64conv_c64u.py
docs/        Todo-List.md, SDK path note, c64 programming books
build/       cmake output, gitignored
run_vice.sh
CMakeLists.txt
```

| Module                                              | Role                                                                                   |
|-----------------------------------------------------|----------------------------------------------------------------------------------------|
| `main.c`                                            | Loop: `cbm_k_getin`, then key dispatch                                                 |
| `editor.c`                                          | Insert, delete, return, page swap                                                      |
| `editor_state.c`                                    | Globals: buffer, cursor, filename, BASIC flag, keyword list                            |
| `screen.c`                                          | Draw, keyword and mark colors, cursor, status line. Dispatches the bitmap to 80-column |
| `screen80.c`                                        | VIC bank 3 bitmap mode, 4x8 font, per-cell color                                       |
| `file_ops.c`                                        | Directory, load, save, drive select, new file                                          |
| `reu.c`                                             | REU detect, size probe, page DMA                                                       |
| `search.c` `clipboard.c` `undo.c` `goto.c` `help.c` | Find/replace, mark/copy/paste, one-line undo, goto, help                               |

Shared constants live in `include/whisper64.h`. KERNAL I/O goes through `cbm_k_*` (`cbm.h`). Hardware poking uses the `POKE` / `PEEK` macros.

## Memory map

The current link (`build/whisper64.map`, regenerated on each build) is approximately:

| Region                | Use                                                                                                                                                                                                                                                                                       |
|-----------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `$0801`               | LLVM-MOS BASIC stub (`SYS` to the program)                                                                                                                                                                                                                                                |
| `$080D`–about `$A000` | Code. BASIC ROM is unmapped at startup (`unmap-basic`), so code may sit under `$A000`                                                                                                                                                                                                     |
| about `$B691`–`$CB18` | BSS. `lines[]` is at `$BBAD`: 48 × 80 = 3840 bytes                                                                                                                                                                                                                                        |
| `$CB38`               | Heap start (`__heap_start`)                                                                                                                                                                                                                                                               |
| `$D000`               | Software stack (`__stack`). Grows down. About 1224 bytes between heap and stack on this link. `shift_rows_40`, `shift_rows_80`, and `shift_viewport` in `screen.c` are `noinline` because inlining them put `__bss_end` at `$CC32`. Stop adding editor code if `__bss_end` passes `$CC00` |
| `$0100`–`$01FF`       | 6502 hardware stack, still in use                                                                                                                                                                                                                                                         |
| `$0400`–`$07E7`       | 40-column screen RAM                                                                                                                                                                                                                                                                      |
| `$D800`–`$DBE7`       | With I/O mapped: color SRAM. With I/O mapped out: RAM that 80-column mode uses as the VIC video matrix                                                                                                                                                                                    |
| `$E000`–`$FF3F`       | 80-column bitmap, under KERNAL ROM. VIC sees it. The CPU sees it only while HIRAM is clear                                                                                                                                                                                                |
| `$DF00`–`$DF0A`       | REU registers. I/O must be mapped                                                                                                                                                                                                                                                         |

Do not grow `lines[]`, the undo buffers, or the clipboard without checking `__bss_end` against `__stack`. A larger `MAX_LINE_LENGTH` multiplies by `LINES_PER_PAGE` (48). Crossing `$D000` collides with the stack.

`screen80_begin_draw` does `SEI` and clears LORAM, HIRAM, and CHAREN (`$01 & $F8`). Inside that window the KERNAL is gone, I/O is gone (so `$D800` writes hit DRAM, which is what VIC bank 3 reads), and `$E000` is the bitmap. Do not call KERNAL, `printf`/`sprintf`, REU code, or anything that needs an IRQ from inside `begin_draw`/`end_draw`. Nested begin/end is reference-counted. The draw code itself must stay below `$D000`. The linker already does that.

`tool_lowercase` in the three tools sends `CHROUT` `$0E`, so those programs use the lowercase/uppercase set. The editor does not send it. 40-column output goes through `to_screen` in `screen.c`. `@` is screen `$00`, `[` is `$1B`, `]` is `$1D`, `|` is `$5D`, and `a`–`z` are `$01`–`$1A`. `$20`–`$3F` and `A`–`Z` (`$41`–`$5A`) already match that set. Braces, backslash, caret, underscore, backtick, and tilde are not in the character ROM. Those bytes are drawn unchanged so they stay distinct from the brackets. The buffer keeps the original bytes. Do not write screen codes back into `lines[]`, and do not translate bytes `$80` and up. Those are graphic PETSCII and must round-trip unchanged. 80-column mode indexes `font_4x8` by `ch - 32` and does not use `to_screen`. Letters the user types arrive from `GETIN` as PETSCII `$41`–`$5A` (unshifted, shown as capitals).

## How the editor stores text

One page is `lines[LINES_PER_PAGE][MAX_LINE_LENGTH]` with `LINES_PER_PAGE` 48 and `MAX_LINE_LENGTH` 80 (79 characters plus a NUL). `num_lines` is the live line count on this page. `current_page` / `num_pages` / `total_lines` cover the whole file.

Other pages live in the REU when one is present, otherwise in drive temp files `$W$.P0`, `$W$.P1`, ... as SEQ. `page_modified` gates the write. REU pages start at offset 256. Each page is a 4-byte header (`magic` `$C64E`, line count) plus the raw `lines` array. Page size is about 3844 bytes. A 512KB REU holds about 136 pages. The C64 Ultimate's REU is larger. The size probe in `reu_get_size` writes one byte at address 0 of every 64KB bank. Call it only at startup, before any page is saved.

There is no horizontal scroll. A line stays one editor line, up to 79 characters. `edit_width` is 37 in 40-column mode and 77 in 80-column mode. The screen draws that many characters per row and continues the same line on the next screen row, with a blank gutter. Return is what starts a new editor line. Do not split the buffer at `edit_width`. That used to leave the tail unnumbered, so a tokenized save dropped it.

The cyan gutter (`00:`) is the row index inside the current page, two digits, not a BASIC line number and not a file-wide line number. The title bar's `line:col` is the file-wide row (`page * 48 + row`). BASIC line numbers, when present, are decimal text at the start of the line: optional spaces, then digits, then a space or the end of the line. `10PRINT` is not a numbered line. The editor does not parse or rewrite those numbers.

`new_line` can open the next page only when Return is pressed on the last row of a full page. Inserting in the middle of a full page shows `PAGE FULL`. Undo stores one line. The clipboard stores two lines.

CTRL+N (PETSCII 14) loads the next page and CTRL+B (PETSCII 2) loads the previous page. Both put the cursor on row 0. Up from row 0 and Down from the last row still change pages too. CTRL+G asks for the title bar's file-wide row, 1-based. Page is `(n - 1) / 48` and the row is the remainder. The row has to be one of the lines stored on that page. A join drops `total_lines` and leaves the later pages at the same title numbers, so the check is that page's `num_lines`, not `total_lines`. A missing row shows `LINE n NOT FOUND` and does not keep the page change.

In BASIC mode the 80th character is not stored and the status row says `BASIC LINE FULL`. Non-BASIC mode stays silent at the same cap. Do not raise `MAX_LINE_LENGTH`. A rejected character must not call `update_current_line`: a 79-character line is wrapped, and that redraw clears the status row.

## Screen modes

`screen_mode` is `MODE_40COL` (0) or `MODE_80COL` (1). CTRL+D toggles it (`main.c`, key code 4) and updates `edit_width` / `screen_width`.

40-column mode writes screen RAM `$0400` and color RAM `$D800` directly. `cputc_at`, `cputs_at`, and the cursor all pass the byte through `to_screen` first. `draw_text_line` scans the line when `basic_mode` is set. Statements are purple and functions are blue. A quote toggles string mode. A finished word `REM` colors that word and stops the scan for the rest of the editor line. `(` ends a word; only `TAB(` and `SPC(` keep the parenthesis. Marked text is yellow, and a keyword color outranks it. The names live in `editor_state.c`. They are the V2 statement and function names, not the token bytes in `src/conv/token.c`. One-character operators are not in the list.

80-column mode is VIC bank 3 (`$DD00` bits 0–1 = 0), bitmap at `$E000`, video matrix at `$D800` (`$D018 = $68`), hires bitmap enabled. The font is the 4x8 SCREEN-80 glyph set in `include/font_4x8.h` (96 glyphs, ASCII 32–127, pixels in the high nibble). Two glyphs share one 8x8 cell, so they share one foreground color. `draw_text_line` fills a 40-byte cell-color array and `render_line_80` stores `color << 4` in that cell. The background nybble stays 0 (black). Cell 0, the two gutter digits, is forced cyan. The colon sits in cell 1 with the first text character and takes that character's color. Keyword purple outranks mark yellow, which outranks white, so a keyword or a mark on either half colors both glyphs. The scan matches 40-column mode. `is_basic_keyword` returns the color. Purple and blue both outrank mark yellow, which outranks white, and the later keyword wins on a shared cell. Typing goes through `draw_text_line`. `cputc_at_80` still paints a whole cell one color; the title and status strings use that.

The cursor in 80-column mode XORs the glyph's nibble in the bitmap. In 40-column mode it sets reverse video on the screen code.

`redraw_screen` blanks title row 0 across `screen_width`, then paints the name, line:col, tags, page, and drive. It also rebuilds every visible line and the status row. A line longer than `edit_width` takes more than one screen row, and the cursor row follows that. `update_current_line` rebuilds the title position, the current line, and the cursor while the line fits on one row. A wrap uses the full redraw. Arrow keys call `refresh_move`. When the viewport and the phantom row stay put, that restores the previous cursor cell, rewrites the 9-column line:col field, and draws the cursor. A scroll moves the editor rows and redraws the band that entered. Mark, a page change, a phantom-row change, and a jump of a full screen still call `update_cursor`. Arrow keys leave the status row alone.

Directory, help, and the save prompt are laid out for 40 columns. In 80-column mode `cputs_at` still draws them, on the left side of the bitmap.

## Files

F1 opens the directory on `current_drive` (default 8, F3 selects 8–15). The listing is a KERNAL load of `$`. File type comes from the directory text (`PRG`, `SEQ`, `USR`, `REL`, `DEL`), with a filename-extension fallback in `ext_table`. A filename that itself contains `PRG` or `SEQ` can be mislabeled.

Load opens the file as a data channel (secondary address 2), not with `LOAD`, so nothing is written over the editor. SEQ uses `name,S,R`. Everything else uses `name,P,R`. Both paths read raw bytes and split on CR or LF into `lines[]`, paging every 48 lines. A tokenized BASIC PRG is not detokenized. Its link pointers, binary line numbers, and token bytes show up as garbage. Bytes `$0D` inside those binary fields also split lines.

F2 always writes SEQ: `@0:name,S,W`, every page. Every SEQ line, including the last, ends with CR. Load keeps the CHRIN byte that arrives with STATUS bit 6, then stops. The following CHRIN is a synthetic CR and is not a line. The overwrite check opens `name,S,R` only, so an existing PRG of the same name is invisible to it. There is no tokenized writer.

## BASIC mode today

F4 turns `basic_mode` on (`BASIC MODE ON`) and off (`BASIC MODE OFF`). Highlighting and the 79-character cap are the only editor effects. The editor does not rewrite line numbers.

`W64RENUM` renumbers a whole SEQ. The step prompt defaults to 10 when left empty. `W64LABEL` numbers labeled lines at step 10 and writes a PRG. The store walks live in `src/conv/renumber.c`.

## Pointer

The 1351 and the port-1 joystick pointer were removed so display wrap fits under `$D000`. CTRL+J is not a command. Do not put `src/mouse.c` back unless a later link still leaves slack between `__bss_end` and `__stack`. If it comes back, sample port 1 (`$DC01`) only. Port 2 (`$DC00`) shares the keyboard columns. A 1351 right button is the joystick UP line.

## Style

Match the surrounding file. C functions, globals in `editor_state`, KERNAL calls spelled `cbm_k_*`, hardware addresses as named macros next to the code that uses them. Comments belong on VIC, CIA, REU, and banking constraints, not on ordinary control flow. Prefer editing the module that already owns the behavior (`src/conv/token.c` and `src/conv/renumber.c` for BASIC text, `file_ops.c` for open/read/write, `screen.c` / `screen80.c` for color) over a new wrapper. The editor builds status text with `sprintf` into a small stack buffer. The converter does not. No `malloc` in the editor.

## Pitfalls

- Do not `LOAD` a user program into memory while the editor is running. The editor occupies `$0801` upward.
- Do not call the KERNAL between `screen80_begin_draw` and `screen80_end_draw`.
- Do not treat `$D800` in 80-column mode as color SRAM. The video matrix is RAM under I/O. Writes with I/O mapped in go to the wrong chip.
- Two 4-pixel characters share one color. Per-character highlighting is not possible. Per-cell coloring is. On 80-column text lines, keyword purple beats mark yellow, which beats white, and cell 0 (the gutter digits) is forced cyan. The colon shares cell 1 with the first text character.
- `reu_get_size` destroys byte 0 of each REU bank. Do not run it after pages have been saved.
- REU DMA needs the `DMA_BARRIER()` already in `reu.c`, or the compiler will delete the transfers.
- Temp-file paging and REU paging must stay in lockstep. Save and load already re-`CHKOUT` after each page reload because opening the temp file disturbs the output channel.
- The gutter, the title-bar line, and the BASIC line number in the text are three different numbers.
- `redraw_screen` blanks title row 0 across `screen_width` before painting the name, line:col, tags, page, and drive. `update_current_line` does not. It rewrites line:col in place as 9 columns. A shorter filename has to go through the full redraw or the tail of the previous name stays.
- Do not split a line at `edit_width`. Display wrap is a screen row only. The gutter number stays on the first row of that editor line.
- The editor highlight list is the V2 statement and function names, purple and blue. The token table lives only in `src/conv/token.c`. Do not merge them. Do not treat `(` as a word character. `TAB(` and `SPC(` are the exception.
- `tools/c64u.py` only understands the 40-column screen.
- Do not divide or take a remainder by `edit_width`. The width is a runtime value, so `/` and `%` pull in `__udiv` / `__divmod` / `__mul`. A subtract loop is rewritten into that divide. Wrap uses two compares. A 79-character line is at most three screen rows. LTO stores `edit_width` and `screen_width` as a one-byte mode flag (0 = 40-column, 1 = 80-column). Readers turn that into 37/77 and 40/80. Do not poke 37 into `edit_width`.
- Do not restore the mouse or joystick without checking that slack. The main loop does not read `$DC00` or `$DC01`.

## Todo-List feasibility

80-column BASIC highlighting, display wrap, the 40-column screen-code map, quote/REM colors, the two-color V2 name list, file-wide goto, the page keys, the BASIC line-full warning, and the second help page are done. The editor does not renumber. Full-file renumber is `W64RENUM`. Labels are `W64LABEL`. SEQ/PRG conversion is `W64CONV`, separate from the editor. The mouse and the joystick pointer were removed for size.

### 80-column BASIC highlighting — done

The rules live in the Screen modes section. The names in `editor_state.c` are the V2 statement and function names. The token bytes stay in `src/conv/token.c`. Do not merge them.

### Display wrap — done

The rules live in "How the editor stores text". One editor line, up to 79 characters. The screen continues it on the next row with a blank gutter. Return is the only new editor line. A file that was already split at column 37 stays split until Delete at the start of the continuation.

### 40-column screen codes — done

`to_screen` in `src/screen.c` is the whole map. It runs on the 40-column draw and the reverse-video cursor. Braces, backslash, caret, underscore, backtick, and tilde stay raw screen codes. A RAM charset for those seven glyphs would need a 2KB VIC-visible hole and would move `__bss_end` up by that much. Do not add that without checking the stack gap.

### Mouse and joystick — removed

Removed so wrap fits under `$D000`. The port-select item is moot. Do not put the pointer back without a size check.

## W64CONV, W64RENUM, and W64LABEL

Three programs replace the single converter. None of them is linked into the editor. The editor saves SEQ only. `include/w64toolui.h` declares the shared screen, directory, drive, probe line, and REU flush. `src/conv/toolui.c` defines them. `src/conv/reu_dma.c` and `include/w64reu.h` are linked into all three and are not linked into the editor. The editor's `src/reu.c` stays the page store. `include/w64tools.h` still holds the result codes. Each CMake target also compiles `src/conv/tool_reu.c`.

| Program | Sources |
|---|---|
| `w64conv.prg` | `src/conv/conv_main.c`, `src/conv/toolui.c`, `src/conv/token.c`, `src/conv/reu_dma.c`, `src/conv/tool_reu.c`, `src/conv/basic_zp.S` |
| `w64renum.prg` | `src/conv/renum_main.c`, `src/conv/toolui.c`, `src/conv/renumber.c`, `src/conv/reu_dma.c`, `src/conv/tool_reu.c`, `src/conv/basic_zp.S` |
| `w64label.prg` | `src/conv/label_main.c`, `src/conv/toolui.c`, `src/conv/renumber.c`, `src/conv/token.c`, `src/conv/reu_dma.c`, `src/conv/tool_reu.c`, `src/conv/basic_zp.S` |

All three are `-Oz`, `--gc-sections`, and `-lexit-return`. A MinSizeRel cache still passes `-Os` first. The target `-Oz` comes after it and wins. Do not compile these three at `-Os`. `basic_zp.S` is compiled as C. The October 24, 2026 maps put `__heap_start` at `$4B2C` (`w64conv`), `$622C` (`w64renum`), and `$907D` (`w64label`). Each has `__stack` at `$D000`. Keep the heap at or below `$C800`. Do not add a RAM file buffer, and do not raise the directory list past 144 entries.

`W64CONV` menu `1` is SEQ to PRG and `2` is PRG to SEQ. `W64RENUM` renumbers a SEQ. An empty step is 10. `W64LABEL` numbers labels at step 10 and writes a PRG, and it does not ask for a step. Read drive and write drive are separate, 8–15, and an empty entry is 8. The directory is its own list: 144 entries, 18 bytes each, name in 0–15, NUL at 16, type at 17. It is discarded when the file is read. `W64CONV` lists SEQ for SEQ to PRG and PRG for PRG to SEQ. `W64RENUM` and `W64LABEL` list SEQ. Skip the disk-header entry. `NO FILES` waits for a key and does not set `verify_code`, so the menu redraws and does not ask ANOTHER.

After the output name is stored, `tool_prepare` blanks rows 2–19. Row 0, row 1, and row 20 (`OUTPUT FILENAME`) stay. Stop during the name prompt returns before that blanking. `BAD NAME` is drawn after it. Progress, `OVERWRITE`, `WROTE`, `RENUMBERED`, and `tool_finish` then land on the cleared rows.

The screen is border gray 12, background black, text green 5, and the selected directory row yellow 7. The centered title on row 0 is the program name: `W64CONV` (7 characters), `W64RENUM`, or `W64LABEL`. `tool_center_row` blanks columns 0–38 and starts the text at `(40 - length) >> 1`, so those names start at column 16. Help titles are centered on row 0 the same way: `W64CONV HELP` at column 14, `W64RENUM HELP` and `W64LABEL HELP` at column 13. Menu lines and the help glossary stay left-aligned. `draw_title` still copies the 8 bytes at columns 16–23 for the file list. This SDK has no `conio.h`. The tool writes `$0400` and color RAM directly and reads keys with `cbm_k_getin`. It does not use `sprintf`. Error text from `tool_put_err` is red (color 2). `CANCELLED` stays green.

Row 1 shows `REU: NONE` or `REU: nKB`, centered by `tool_center_row`. The largest value is `16320KB`. `REU: NONE` starts at column 15. `REU: 16320KB` starts at column 14. A job needs at least `128KB`. The menu, the directory, the drive prompts, and help work at any measured size. Choosing the job when the size is under 131072 bytes paints `REU 128KB REQUIRED` and waits for a key, then returns to the menu. The probe runs once, in `main`, before the menu and before any file bytes are stored. It overwrites byte 0 of every bank it touches. After it runs, that program owns the REU until it exits, including unsaved editor pages. Do not probe again.

The file is stored in the REU. `TOO BIG` means that REU filled. A tokenized program can still stop at `PRG TOO BIG` when a 16-bit link would wrap. Line numbers stop at 63999, and a line stops at 79 characters. PRG to SEQ keeps a longer line only through the last colon outside quotes, `REM`, and quoted `DATA`. The status line is `SEQ n TRUNC t ML m`: `t` lines lost that tail, and `m` is 1 when bytes follow the BASIC program. Help rows 18 and 19 say so. A first statement that does not fit stops the job with `LINE LONG` and that line number. The SEQ is not written. `W64CONV` uses one region at address 0. `W64RENUM` and `W64LABEL` keep the source SEQ at address 0, grow the output upward from the end of that SEQ, and grow the map downward from the measured end. `W64LABEL` writes the PRG above the numbered SEQ. A renumber record is 6 bytes, packed little-endian. A label record is 14 bytes. On the C64 `unsigned int` is 16 bits, so do not assume the host's 16-byte `LabelEnt`.

A 1541 has one data channel. Close the directory before opening the file. Do not open the output while the input is still open. The overwrite probe reads the DOS error channel. `cbm_k_open` returns 0 for a missing file; DOS code 62 means the name is free. A new file is `name,P,W` or `name,S,W`. `@0:` is only when the probe finds the name already there.

New numbers are the step, then twice the step, and they stop at 63999. Check `63999u - step` before adding so a 16-bit step does not wrap. The store path returns 0, or 1 (step 0 or above 63999), 2 (a new or old number would pass 63999), 3 (the next byte would enter another region, or pass the measured end), 4 (a line would pass 79 characters), 5 (a GOTO, GOSUB, THEN, ELSE, GO TO, or GO SUB number is not in the map). The screen stores 3 as `TOO BIG` and 5 as `NO NUM`. `tokenize_store` returns 9 when the next 16-bit BASIC link would wrap, and the screen stores that as `PRG TOO BIG`. A collision with the map or the end of the REU stays 3. Label codes add 6 (unknown name on GOTO or GOSUB), 7 (name longer than 11), and 8 (duplicate). The screen stores 6 as `NO LABEL`, 7 as `BAD NAME`, and 8 as `DUP LABEL`. Code 4 and code 6 append the 1-based source row when it is non-zero (`LINE LONG 12`, `NO LABEL 12`). A letter starts a label, then letters or digits, at most 11, then `:`. One space after the colon is stripped. A label with nothing after the colon shares the next code line's number, and that label line is not emitted. A blank line, or the end of the file, gives each of those labels its own empty numbered line. Digit-leading lines keep their numbers and are not labels. `THEN` and `ELSE` rewrite a mapped name, or a `GOTO` / `GOSUB` target. A V2 keyword after `THEN` or `ELSE` stays a statement, even when a label has that keyword's name. `GO TO` and `GO SUB` are renumber keywords, not label keywords.

`renumber_seq`, `labels_to_numbered`, and the RAM tokenizer stay in `src/conv/renumber.c` for the host tests. `W64RENUM` calls `renumber_seq_store`. `W64LABEL` calls `labels_to_numbered_store`, then `tokenize_store`. `W64CONV` tokenizes and detokenizes in `src/conv/conv_main.c`. `--gc-sections` leaves the RAM entry points out of the PRGs.

KERNAL STATUS at `$90` is OR-ed. `chrin_fresh` clears it before each `CHRIN`. An EOF bit keeps that byte. A bare timeout is the end of the file. Renumber and labels split lines on CR only. `W64CONV` splits a SEQ on CR or LF.

N or Stop (PETSCII 3) at the another-file prompt does not return from `main`. `tool_exit_ready` in `src/conv/basic_zp.S` calls RESTOR, then CLOSE on `LAT[0]` until `$98` is 0 (at most 16). JiffyDOS CLALL (`$FFE7`) only zeros `$98` and does not send CLOSE. A data channel left open on the 1541 makes the next LOAD time out, BASIC prints `?FILE NOT FOUND ERROR`, and the program at `$0801` stays. After those CLOSEs it UNTALKs, UNLISTENs, CLALLs, and CINTs, sets border 14, background 6, `$D018` to `$15`, and `$0286` to 14, and ORs `$10` into `$DD00` so the clock is released. It does that again after 15 is closed. Idle after a reset is `$97`. `$C7` after a successful JiffyDOS LOAD is normal. It then opens logical file 15 on the write drive (else the read drive, else 8) with the DOS command `I` at `$033C`, clears `$90` before each CHRIN, drains until CR, closes 15, and sets FNLEN to 0. It stores the drive in `$BA`, copies the `.init.005` image of `$02`–`$8F` back, stores `$37` in `$01`, and jumps through the warm-start vector at `$A002`. Do not call IOINIT. `.init.005` still saves `$02`–`$8F`, which is where stock CHRGET and TEMPPT live. Do not return through `__exit_return`: after `.fini.989` it stores `__rc16` over `$12` and `__rc20` over `$16`. `$16` is TEMPPT, and a filename is a string, so LOAD breaks while PRINT of a number can still work. `tool_lowercase` is what leaves the lowercase/uppercase set selected; CINT and `$D018 = $15` put the uppercase/graphics set back. `-lexit-return` stays linked. Do not link `save-basic.o`: it also defines `_Exit`. CMake's `project(whisper64 C)` ignores a `.S` file unless `set_source_files_properties` sets `LANGUAGE C`. Menu Stop still says cancelled and waits. The editor never takes this path.

The token table in `src/conv/token.c` is V2 token order and is separate from the editor highlight list. Do not merge them. Spaces between the line number and the first statement byte are not stored. LIST puts that space back. Host checks are `gcc -Wall -Wextra -Iinclude -o /tmp/test_renumber tools/test_renumber.c src/conv/renumber.c src/conv/token.c`, `gcc -Wall -Wextra -Iinclude -o /tmp/test_bigfile tools/test_bigfile.c src/conv/renumber.c src/conv/token.c`, `gcc -Wall -Wextra -Iinclude -o /tmp/test_format_line tools/test_format_line.c src/conv/token.c`, and `python3 tools/test_basic_prg.py`. A disk round trip is `tools/test_w64conv_c64u.py` on the C64 Ultimate. The script name stayed. Do not treat a VICE run as that test, and do not run the script unless asked. It resets the Ultimate.

Do not move the tokenizer back into the editor. That attempt put `__bss_end` at `$CF3D` (195 bytes under `$D000`) and the 80-column draw no longer fit. The current editor link has `__bss_end` at `$CB18`, `__heap_start` at `$CB38`, and `__stack` at `$D000`. Help page 2 names `W64CONV`, `W64RENUM`, and `W64LABEL` and is kept short on purpose. Do not lengthen those strings without checking the map.
