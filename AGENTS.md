# AGENTS.md — Coding Agent Guidelines for Whisper64-Extended

**Project:** Whisper64-Extended — a text editor for the Commodore 64, aimed at real hardware and especially the C64 Ultimate. Forked from https://github.com/sandlbn/whisper64. This repo is https://github.com/RapidFire770/whisper64-extended.
**Primary language:** C, cross-compiled with the LLVM-MOS SDK (`c64` platform). No Rust, no extra crates.
**Status:** Editing, paging, SEQ load/save, BASIC highlighting in 40- and 80-column mode, current-page renumber, and display wrap. Tokenized BASIC V2 SEQ/PRG conversion is `w64conv.prg`, not part of the editor. 80-column keyword color is one foreground color per hires cell (two glyphs share it). Mouse and joystick support was removed so the editor stays under `$D000`. Open work is `docs/Todo-List.md`.
**Last Updated:** October 15, 2026

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

---

## Toolchain

LLVM-MOS SDK on this machine:

```text
/home/josh/Documents/Programming/C64/tools/llvm-mos
```

That path is also in `docs/llvm-mos-sdk location.md`, which is gitignored. The README's `~/llvm-mos` prefix is the upstream example, not this machine.

```bash
cd build
cmake -DCMAKE_PREFIX_PATH=/home/josh/Documents/Programming/C64/tools/llvm-mos ..
make
```

`CMakeLists.txt` sets `LLVM_MOS_PLATFORM` to `c64` before `project()`. Output is `build/whisper64.prg` and `build/w64conv.prg`. Flags are `-Os`, `-ffunction-sections`, `--gc-sections` on both. A 512KB `whisper64.reu` image and `whisper64.d64` are created by the build and by `run_vice.sh`. Both are gitignored, as is `build/`.

Run in VICE:

```bash
./run_vice.sh
```

That starts `x64sc` with REU enabled, the REU image writable, and the D64 on drive 8. On the C64 Ultimate, `LOAD "WHISPER64",8,1` then `RUN`. `tools/c64u.py` drives the Ultimate's REST API. The host is hardcoded to `http://192.168.100.193`. It injects keys through the KERNAL keyboard buffer at `$0277` / `$C6` and reads the 40-column screen at `$0400`. It cannot see 80-column bitmap mode.

There is no unit-test target. Do not claim a C64 behavior works unless VICE or the Ultimate showed it. A host Python script is the right place for a tokenizer round-trip test. Do not add a crate for that.

## Layout

```text
include/     headers, one per module, plus font_4x8.h, whisper64.h, w64conv.h
src/         editor .c files
src/conv/    w64conv tokenize and disk streaming
tools/       c64u.py (Ultimate REST driver), test_basic_prg.py, test_w64conv_c64u.py
docs/        Todo-List.md, SDK path note, c64 programming books
build/       cmake output, gitignored
run_vice.sh
CMakeLists.txt
```

| Module | Role |
|---|---|
| `main.c` | Loop: `cbm_k_getin`, then key dispatch |
| `editor.c` | Insert, delete, return, page swap |
| `editor_state.c` | Globals: buffer, cursor, filename, BASIC flag, keyword list |
| `screen.c` | Draw, keyword and mark colors, cursor, status line. Dispatches the bitmap to 80-column |
| `screen80.c` | VIC bank 3 bitmap mode, 4x8 font, per-cell color |
| `file_ops.c` | Directory, load, save, drive select, new file |
| `basic.c` | Line-number parse and renumber of the current page |
| `reu.c` | REU detect, size probe, page DMA |
| `search.c` `clipboard.c` `undo.c` `goto.c` `help.c` | Find/replace, mark/copy/paste, one-line undo, goto, help |

Shared constants live in `include/whisper64.h`. KERNAL I/O goes through `cbm_k_*` (`cbm.h`). Hardware poking uses the `POKE` / `PEEK` macros.

## Memory map

The current link (`build/whisper64.map`, regenerated on each build) is approximately:

| Region | Use |
|---|---|
| `$0801` | LLVM-MOS BASIC stub (`SYS` to the program) |
| `$080D`–about `$A000` | Code. BASIC ROM is unmapped at startup (`unmap-basic`), so code may sit under `$A000` |
| about `$AFDC`–`$C660` | BSS. `lines[]` is at `$B6F8`: 48 × 80 = 3840 bytes |
| `$C660` | Heap start |
| `$D000` | Software stack (`__stack`). Grows down. About 2.4KB between heap and stack |
| `$0100`–`$01FF` | 6502 hardware stack, still in use |
| `$0400`–`$07E7` | 40-column screen RAM |
| `$D800`–`$DBE7` | With I/O mapped: color SRAM. With I/O mapped out: RAM that 80-column mode uses as the VIC video matrix |
| `$E000`–`$FF3F` | 80-column bitmap, under KERNAL ROM. VIC sees it. The CPU sees it only while HIRAM is clear |
| `$DF00`–`$DF0A` | REU registers. I/O must be mapped |

Do not grow `lines[]`, the undo buffers, or the clipboard without checking `__bss_end` against `__stack`. A larger `MAX_LINE_LENGTH` multiplies by `LINES_PER_PAGE` (48). Crossing `$D000` collides with the stack.

`screen80_begin_draw` does `SEI` and clears LORAM, HIRAM, and CHAREN (`$01 & $F8`). Inside that window the KERNAL is gone, I/O is gone (so `$D800` writes hit DRAM, which is what VIC bank 3 reads), and `$E000` is the bitmap. Do not call KERNAL, `printf`/`sprintf`, REU code, or anything that needs an IRQ from inside `begin_draw`/`end_draw`. Nested begin/end is reference-counted. The draw code itself must stay below `$D000`. The linker already does that.

Startup prints PETSCII through direct screen stores and through the 4x8 font indexed by `ch - 32`. The C64 is in the lowercase/uppercase character set, so buffer bytes `$20`–`$7E` match both ASCII and screen codes for those glyphs. Letters the user types arrive from `GETIN` as PETSCII `$41`–`$5A` (unshifted, shown as capitals). Do not add a second screen-code conversion. Bytes `$80` and up are graphic PETSCII and must round-trip unchanged.

## How the editor stores text

One page is `lines[LINES_PER_PAGE][MAX_LINE_LENGTH]` with `LINES_PER_PAGE` 48 and `MAX_LINE_LENGTH` 80 (79 characters plus a NUL). `num_lines` is the live line count on this page. `current_page` / `num_pages` / `total_lines` cover the whole file.

Other pages live in the REU when one is present, otherwise in drive temp files `$W$.P0`, `$W$.P1`, ... as SEQ. `page_modified` gates the write. REU pages start at offset 256. Each page is a 4-byte header (`magic` `$C64E`, line count) plus the raw `lines` array. Page size is about 3844 bytes. A 512KB REU holds about 136 pages. The C64 Ultimate's REU is larger. The size probe in `reu_get_size` writes one byte at address 0 of every 64KB bank. Call it only at startup, before any page is saved.

There is no horizontal scroll. A line stays one editor line, up to 79 characters. `edit_width` is 37 in 40-column mode and 77 in 80-column mode. The screen draws that many characters per row and continues the same line on the next screen row, with a blank gutter. Return is what starts a new editor line. Do not split the buffer at `edit_width`. That used to leave the tail unnumbered, so a tokenized save dropped it.

The cyan gutter (`00:`) is the row index inside the current page, two digits, not a BASIC line number and not a file-wide line number. The title bar's `line:col` is the file-wide row (`page * 48 + row`). BASIC line numbers, when present, are decimal text at the start of the line. `extract_line_number` requires digits then a space or end of line.

`new_line` can open the next page only when Return is pressed on the last row of a full page. Inserting in the middle of a full page shows `PAGE FULL`. Undo stores one line. The clipboard stores two lines.

## Screen modes

`screen_mode` is `MODE_40COL` (0) or `MODE_80COL` (1). CTRL+D toggles it (`main.c`, key code 4) and updates `edit_width` / `screen_width`.

40-column mode writes screen RAM `$0400` and color RAM `$D800` directly. `draw_text_line` scans the line, and when `basic_mode` is set it colors a completed word purple if `is_basic_keyword` matches. Marked text is yellow. The keyword table in `editor_state.c` is a short subset (PRINT, FOR, GOTO, ...). It is not the BASIC V2 token list. A second, different list lives inside `replace_line_number` in `basic.c`.

80-column mode is VIC bank 3 (`$DD00` bits 0–1 = 0), bitmap at `$E000`, video matrix at `$D800` (`$D018 = $68`), hires bitmap enabled. The font is the 4x8 SCREEN-80 glyph set in `include/font_4x8.h` (96 glyphs, ASCII 32–127, pixels in the high nibble). Two glyphs share one 8x8 cell, so they share one foreground color. `draw_text_line` fills a 40-byte cell-color array and `render_line_80` stores `color << 4` in that cell. The background nybble stays 0 (black). Cell 0, the two gutter digits, is forced cyan. The colon sits in cell 1 with the first text character and takes that character's color. Keyword purple outranks mark yellow, which outranks white, so a keyword or a mark on either half colors both glyphs. The scan matches 40-column mode: `is_basic_keyword` against the short list in `editor_state.c`, quote-blind, and a finished keyword is painted purple even when that span is also marked. Typing goes through `draw_text_line`. `cputc_at_80` still paints a whole cell one color; the title and status strings use that.

The cursor in 80-column mode XORs the glyph's nibble in the bitmap. In 40-column mode it sets reverse video on the screen code.

`redraw_screen` blanks title row 0 across `screen_width`, then paints the name, line:col, tags, page, and drive. It also rebuilds every visible line and the status row. A line longer than `edit_width` takes more than one screen row, and the cursor row follows that. `update_current_line` rebuilds the title position, the current line, and the cursor while the line fits on one row. A wrap uses the full redraw. Arrow keys use the full redraw.

Directory, help, and the save prompt are laid out for 40 columns. In 80-column mode `cputs_at` still draws them, on the left side of the bitmap.

## Files

F1 opens the directory on `current_drive` (default 8, F3 selects 8–15). The listing is a KERNAL load of `$`. File type comes from the directory text (`PRG`, `SEQ`, `USR`, `REL`, `DEL`), with a filename-extension fallback in `ext_table`. A filename that itself contains `PRG` or `SEQ` can be mislabeled.

Load opens the file as a data channel (secondary address 2), not with `LOAD`, so nothing is written over the editor. SEQ uses `name,S,R`. Everything else uses `name,P,R`. Both paths read raw bytes and split on CR or LF into `lines[]`, paging every 48 lines. A tokenized BASIC PRG is not detokenized. Its link pointers, binary line numbers, and token bytes show up as garbage. Bytes `$0D` inside those binary fields also split lines.

F2 always writes SEQ: `@0:name,S,W`, every page. Every SEQ line, including the last, ends with CR. Load keeps the CHRIN byte that arrives with STATUS bit 6, then stops. The following CHRIN is a synthetic CR and is not a line. The overwrite check opens `name,S,R` only, so an existing PRG of the same name is invisible to it. There is no tokenized writer.

## BASIC mode today

F4 sets `basic_mode`, which only affects highlighting and the status tag. F4 again asks to renumber. Yes runs `renumber_basic`. No turns BASIC mode off.

Renumber walks the current page only. It maps line numbers to 10, 20, 30, ... and rewrites a following number after `GOTO`, `GOSUB`, `GO TO`, `GO SUB`, `THEN`, and `ELSE`. It does not see other pages, `ON ... GOTO` lists beyond a single number, or numbers written without the keyword patterns above. `line_mappings[]` is 128 entries. A page holds at most 48 numbered lines, so the cap is not the bug. The single-page walk is.

## Pointer

The 1351 and the port-1 joystick pointer were removed so display wrap fits under `$D000`. CTRL+J is not a command. Do not put `src/mouse.c` back unless a later link still leaves slack between `__bss_end` and `__stack`. If it comes back, sample port 1 (`$DC01`) only. Port 2 (`$DC00`) shares the keyboard columns. A 1351 right button is the joystick UP line.

## Style

Match the surrounding file. C functions, globals in `editor_state`, KERNAL calls spelled `cbm_k_*`, hardware addresses as named macros next to the code that uses them. Comments belong on VIC, CIA, REU, and banking constraints, not on ordinary control flow. Prefer editing the module that already owns the behavior (`basic.c` for tokens, `file_ops.c` for open/read/write, `screen.c` / `screen80.c` for color) over a new wrapper. `sprintf` into a small stack buffer is the existing way to build status text. No `malloc` in the editor.

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
- The editor highlight list is a short subset. The V2 token table lives only in `src/conv/token.c`. Do not merge them back into the editor.
- `tools/c64u.py` only understands the 40-column screen.
- Do not divide or take a remainder by `edit_width`. The width is a runtime value, so `/` and `%` pull in `__udiv` / `__divmod` / `__mul`. A subtract loop is rewritten into that divide. Wrap uses two compares. A 79-character line is at most three screen rows. LTO stores `edit_width` and `screen_width` as a one-byte mode flag (0 = 40-column, 1 = 80-column). Readers turn that into 37/77 and 40/80. Do not poke 37 into `edit_width`.
- Do not restore the mouse or joystick without checking that slack. The main loop does not read `$DC00` or `$DC01`.

## Todo-List feasibility

80-column BASIC highlighting and display wrap are done. SEQ/PRG conversion is `w64conv`, not an editor feature. The mouse and the joystick pointer were removed for size. Open items are garbled 40-column characters, REM highlighting, full V2 colors, full-file renumber, and the BASIC line-length limit.

### 80-column BASIC highlighting — done

The rules live in the Screen modes section. The editor list stays the short subset. Do not copy the V2 token table into `editor_state.c`.

### Display wrap — done

The rules live in "How the editor stores text". One editor line, up to 79 characters. The screen continues it on the next row with a blank gutter. Return is the only new editor line. A file that was already split at column 37 stays split until Delete at the start of the continuation.

### Mouse and joystick — removed

Removed so wrap fits under `$D000`. The port-select item is moot. Do not put the pointer back without a size check.

## W64CONV

`src/conv/token.c` and `src/conv/main.c` build `w64conv.prg`. It is not linked into the editor. SEQ to PRG and PRG to SEQ stream one line through KERNAL secondary address 2. The editor saves SEQ only. This SDK has no `conio.h`, so the prompts write `$0400` directly and read keys with `cbm_k_getin`.

The overwrite prompt reads the DOS error channel. `cbm_k_open` returns 0 for a missing file on the Ultimate; DOS code 62 means the name is free. Do not treat a zero open as "file exists". A new file is opened as `name,P,W` or `name,S,W`. `@0:` is only for a name that is already there.

A 1541 has one data channel. The converter reads the input to completion, holds up to 32KB in `out_bytes`, then writes. Do not open the output while the input is still open. KERNAL STATUS at `$90` is OR-ed; `read_byte` clears it before each `CHRIN`. An EOF bit keeps that byte. A bare timeout is the end of the file.

The token table in `src/conv/token.c` is V2 token order and is separate from the editor's highlight list. Do not merge them. Spaces between the line number and the first statement byte are not stored. LIST puts that space back. Host byte checks are `tools/test_basic_prg.py`. A disk round trip is `tools/test_w64conv_c64u.py` on the C64 Ultimate. Do not treat a VICE run as that test.

Do not move the tokenizer back into the editor. That attempt put `__bss_end` at `$CF3D` (195 bytes under `$D000`) and the 80-column draw no longer fit. The current link, with display wrap and without the mouse, has `__bss_end` at `$C660` and `__stack` at `$D000`.
