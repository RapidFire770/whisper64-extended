# AGENTS.md — Coding Agent Guidelines for Whisper64-Extended

**Project:** Whisper64-Extended — a text editor for the Commodore 64, aimed at real hardware and especially the C64 Ultimate. Forked from https://github.com/sandlbn/whisper64. This repo is https://github.com/RapidFire770/whisper64-extended.
**Primary language:** C, cross-compiled with the LLVM-MOS SDK (`c64` platform). No Rust, no extra crates.
**Status:** Editing, paging, SEQ load/save, BASIC highlighting in 40- and 80-column mode, and renumber work. Tokenized BASIC PRG load/save does not. 80-column keyword color is one foreground color per hires cell (two glyphs share it). Mouse mode is a 1351 pointer in 40-column mode only. Open work is `docs/Todo-List.md`.
**Last Updated:** October 11, 2026

This file defines how coding agents should work on the project. It exists to keep changes consistent. AGENTS.md takes priority over any older design notes. The sibling project `~/Documents/Programming/C64/BASIC-IDE` (Kick Assembler) is a separate, partly working attempt. Do not copy it in. Use it only as a record of tokenizer pitfalls.

**IMPORTANT:**

- Always check available skills and invoke them when relevant to the user's request.
- If adding a new crate explain to the user why it is needed or should be used compared to what it will take to do the work without it and give them the option to say yes or no to its inclusion.
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

`CMakeLists.txt` sets `LLVM_MOS_PLATFORM` to `c64` before `project()`. Output is `build/whisper64.prg`. Flags are `-Os`, `-ffunction-sections`, `--gc-sections`. A 512KB `whisper64.reu` image and `whisper64.d64` are created by the build and by `run_vice.sh`. Both are gitignored, as is `build/`.

Run in VICE:

```bash
./run_vice.sh
```

That starts `x64sc` with REU enabled, the REU image writable, and the D64 on drive 8. On the C64 Ultimate, `LOAD "WHISPER64",8,1` then `RUN`. `tools/c64u.py` drives the Ultimate's REST API. The host is hardcoded to `http://192.168.100.193`. It injects keys through the KERNAL keyboard buffer at `$0277` / `$C6` and reads the 40-column screen at `$0400`. It cannot see 80-column bitmap mode.

There is no unit-test target. Do not claim a C64 behavior works unless VICE or the Ultimate showed it. A host Python script is the right place for a tokenizer round-trip test. Do not add a crate for that.

## Layout

```text
include/     headers, one per module, plus font_4x8.h and whisper64.h
src/         matching .c files
tools/       c64u.py (Ultimate REST driver)
docs/        Todo-List.md, SDK path note, c64 programming books
build/       cmake output, gitignored
run_vice.sh
CMakeLists.txt
```

| Module | Role |
|---|---|
| `main.c` | Loop: mouse poll, then `cbm_k_getin`, then key dispatch |
| `editor.c` | Insert, delete, return, page swap |
| `editor_state.c` | Globals: buffer, cursor, filename, BASIC flag, keyword list |
| `screen.c` | Draw, keyword and mark colors, cursor, status line. Dispatches the bitmap to 80-column |
| `screen80.c` | VIC bank 3 bitmap mode, 4x8 font, per-cell color |
| `file_ops.c` | Directory, load, save, drive select, new file |
| `basic.c` | Line-number parse and renumber of the current page |
| `reu.c` | REU detect, size probe, page DMA |
| `mouse.c` | 1351 on control port 1 |
| `search.c` `clipboard.c` `undo.c` `goto.c` `help.c` | Find/replace, mark/copy/paste, one-line undo, goto, help |

Shared constants live in `include/whisper64.h`. KERNAL I/O goes through `cbm_k_*` (`cbm.h`). Hardware poking uses the `POKE` / `PEEK` macros.

## Memory map

The current link (`build/whisper64.map`, regenerated on each build) is approximately:

| Region | Use |
|---|---|
| `$0801` | LLVM-MOS BASIC stub (`SYS` to the program) |
| `$080D`–about `$A000` | Code. BASIC ROM is unmapped at startup (`unmap-basic`), so code may sit under `$A000` |
| about `$AC19`–`$C2A3` | BSS. `lines[]` is the big object: 48 × 80 = 3840 bytes |
| `$C2A3` | Heap start |
| `$D000` | Software stack (`__stack`). Grows down. About 3.4KB between heap and stack |
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

There is no horizontal scroll. `draw_text_line` shows `edit_width` characters from column 0. `edit_width` is 37 in 40-column mode and 77 in 80-column mode. `insert_char` hard-splits the buffer at `edit_width` once the cursor reaches that column. That split is a real extra editor line, not a display wrap. A BASIC logical line broken this way will not survive a tokenized save.

The cyan gutter (`00:`) is the row index inside the current page, two digits, not a BASIC line number and not a file-wide line number. The title bar's `line:col` is the file-wide row (`page * 48 + row`). BASIC line numbers, when present, are decimal text at the start of the line. `extract_line_number` requires digits then a space or end of line.

`new_line` can open the next page only when Return is pressed on the last row of a full page. Inserting in the middle of a full page shows `PAGE FULL`. Undo stores one line. The clipboard stores two lines.

## Screen modes

`screen_mode` is `MODE_40COL` (0) or `MODE_80COL` (1). CTRL+D toggles it (`main.c`, key code 4) and updates `edit_width` / `screen_width`.

40-column mode writes screen RAM `$0400` and color RAM `$D800` directly. `draw_text_line` scans the line, and when `basic_mode` is set it colors a completed word purple if `is_basic_keyword` matches. Marked text is yellow. The keyword table in `editor_state.c` is a short subset (PRINT, FOR, GOTO, ...). It is not the BASIC V2 token list. A second, different list lives inside `replace_line_number` in `basic.c`.

80-column mode is VIC bank 3 (`$DD00` bits 0–1 = 0), bitmap at `$E000`, video matrix at `$D800` (`$D018 = $68`), hires bitmap enabled. The font is the 4x8 SCREEN-80 glyph set in `include/font_4x8.h` (96 glyphs, ASCII 32–127, pixels in the high nibble). Two glyphs share one 8x8 cell, so they share one foreground color. `draw_text_line` fills a 40-byte cell-color array and `render_line_80` stores `color << 4` in that cell. The background nybble stays 0 (black). Cell 0, the two gutter digits, is forced cyan. The colon sits in cell 1 with the first text character and takes that character's color. Keyword purple outranks mark yellow, which outranks white, so a keyword or a mark on either half colors both glyphs. The scan matches 40-column mode: `is_basic_keyword` against the short list in `editor_state.c`, quote-blind, and a finished keyword is painted purple even when that span is also marked. Typing goes through `draw_text_line`. `cputc_at_80` still paints a whole cell one color; the title and status strings use that.

The cursor in 80-column mode XORs the glyph's nibble in the bitmap. In 40-column mode it sets reverse video on the screen code.

`redraw_screen` rebuilds the title, every visible line, and the status row. `update_current_line` rebuilds the title position, the current line, and the cursor. Typing uses the fast path. Arrow keys use the full redraw.

Directory, help, and the save prompt are laid out for 40 columns. In 80-column mode `cputs_at` still draws them, on the left side of the bitmap.

## Files

F1 opens the directory on `current_drive` (default 8, F3 selects 8–15). The listing is a KERNAL load of `$`. File type comes from the directory text (`PRG`, `SEQ`, `USR`, `REL`, `DEL`), with a filename-extension fallback in `ext_table`. A filename that itself contains `PRG` or `SEQ` can be mislabeled.

Load opens the file as a data channel (secondary address 2), not with `LOAD`, so nothing is written over the editor. SEQ uses `name,S,R`. Everything else uses `name,P,R`. Both paths read raw bytes and split on CR or LF into `lines[]`, paging every 48 lines. A tokenized BASIC PRG is not detokenized. Its link pointers, binary line numbers, and token bytes show up as garbage. Bytes `$0D` inside those binary fields also split lines.

F2 always writes SEQ: `@0:name,S,W`, CR between lines, every page. The overwrite check opens `name,S,R` only, so an existing PRG of the same name is invisible to it. There is no tokenized writer.

## BASIC mode today

F4 sets `basic_mode`, which only affects highlighting and the status tag. F4 again asks to renumber. Yes runs `renumber_basic`. No turns BASIC mode off.

Renumber walks the current page only. It maps line numbers to 10, 20, 30, ... and rewrites a following number after `GOTO`, `GOSUB`, `GO TO`, `GO SUB`, `THEN`, and `ELSE`. It does not see other pages, `ON ... GOTO` lists beyond a single number, or numbers written without the keyword patterns above. `line_mappings[]` is 128 entries. A page holds at most 48 numbered lines, so the cap is not the bug. The single-page walk is.

## Mouse

CTRL+J toggles mouse mode. `mouse.c` reads a 1351 on control port 1: movement from SID pots `$D419` / `$D41A`, left button from CIA1 `$DC01` bit 4 (active low). The pointer is screen code 94 written into `$0400`. Coordinates are capped at 40×25. A click in the edit area calls `mouse_to_editor_pos`, which assumes a 3-column gutter and a 40-column screen.

In 80-column mode that pointer is invisible, because VIC is no longer showing `$0400`, and the hit test is still in 40-column coordinates. `MOUSE_BTN_RIGHT` is unused. On a 1351 the right button is usually the joystick UP line (port 1 bit 0).

The main loop is not synced to the raster. It polls the mouse, then calls `GETIN`, and repeats. A held joystick line would fire on every pass unless movement is rate-limited.

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
- `insert_char` splits the buffer at `edit_width`. That corrupts a BASIC logical line.
- Keyword lists in `editor_state.c` and `basic.c` already disagree. A tokenizer needs one V2 table, shared with the highlighter.
- `tools/c64u.py` only understands the 40-column screen.

## Todo-List feasibility

Two open items in `docs/Todo-List.md` remain: tokenized PRG load/save, and joystick mouse movement. 80-column BASIC highlighting is done. Neither remaining item needs a new linker layout, a new crate, or a different editor buffer. Keep the buffer as PETSCII text. That is already the right model. The sibling BASIC-IDE project used the same model and still spent its complexity on token edge cases, not on a second copy of the text.

### 80-column BASIC highlighting — done

The rules live in the Screen modes section. Do not widen `basic_keywords[]` as part of a color tweak. The full V2 list belongs with the tokenizer, and both highlighters must keep using the one table in `editor_state.c`.

### Load and save tokenized BASIC PRG — feasible, the largest of the three

SEQ load/save stays. PRG is a second format on the same text buffer.

On-disk BASIC V2, after the 2-byte load address (normally `$01 $08`):

- next-line pointer, little-endian, absolute if loaded at `$0801`. `00 00` ends the program
- line number, little-endian, 0–63999
- bytes until `00`. `$80`–`$CB` are keywords in token order from `END` through `GO`. `$FF` is PI. Other bytes are PETSCII
- inside quotes, and after a `REM` token, bytes are literal through the closing quote or the `00`, even when they are `>= $80`
- `DATA` is still tokenized outside quotes

Load: keep the data-channel read. If the file is PRG and the first records look like BASIC (load address, rising links, `00` line ends, a final `00 00`), detokenize each line into `"10 PRINT ..."` text and feed the existing pager. If it does not look like BASIC, say so and do not dump tokens into the buffer. Do not `LOAD` over the editor.

Save, when the user asks for a BASIC program: tokenize one source line into a small stack buffer (a 79-character source line only gets shorter), emit the link pointer from a running address that starts at `$0801`, then the binary line number, tokens, and `00`. Finish with `00 00`. Write `name,P,W` (overwrite `@0:name,P,W`). The overwrite probe has to look for the PRG, not only the SEQ. Stream straight to disk. There is not enough free RAM to assemble a whole program (the heap/stack gap is about 3KB).

A line with no leading line number cannot be part of a PRG. Report it and skip or abort that save. Do not invent a number.

Lines that detokenize past 79 characters must be reported and truncated, or the buffer has to grow. Growing it means re-checking `$C2A3` against the stack at `$D000`. Normal C64 lines typed without abbreviations fit. Abbreviation-packed lines sometimes do not.

Do this in the same change, or PRG files will not round-trip:

- In BASIC mode, stop `insert_char` from hard-splitting at `edit_width`. A logical line stays one buffer line, up to 79 characters, even when 40-column mode cannot show all of it.
- Renumber has to walk every page, not only `lines[]`. A real program is often longer than 48 lines.

Out of scope unless the user asks: running the program in place, abbreviation entry (`P` + shifted `R`), and BASIC extensions beyond V2. `?` as `PRINT` is a small optional match because the C64 cruncher accepts it.

### Joystick moves the mouse cursor — feasible, small, 40-column only

The pointer is already a software cursor with its own x/y. A joystick only has to nudge those and reuse the existing click path. No new sprite.

Read control port 1 (`$DC01`, active low, bits 0–3 = up, down, left, right, bit 4 = fire). That port is already an input for the 1351 button, so the keyboard scan can stay alone. Port 2 (`$DC00`) shares CIA1 port A with the keyboard and will drop or ghost keys if it is polled in this tight loop. The C64 Ultimate can map a USB pad onto port 1.

Rate-limit the repeat. The main loop is not a frame tick. Without a delay counter a held direction pins the cursor to the edge immediately. Fire on port 1 is the same bit the 1351 left button uses, so the existing click handler is enough.

A 1351 right button pulls UP. If both devices are not plugged in together, treating bit 0 as UP is correct for a joystick. Document that a 1351 right-click can nudge the pointer up while mouse mode is on.

This does not make the pointer visible in 80-column mode. That needs a bitmap cursor and an 80-column hit test in `mouse_to_editor_pos`. It is a separate change from reading the joystick.
