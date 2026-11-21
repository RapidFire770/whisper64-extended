# Whisper64

A text editor for the Commodore 64.

![Whisper64 Screenshot](screenshot.png)

## Features

- **80-Column Mode**: Bitmap-based 80-column display using a 4x8 pixel font (toggle with CTRL+D)
- **REU Support**: RAM Expansion Unit for fast page swapping (auto-detected, up to 16MB)
- **BASIC Mode** with keyword syntax highlighting (F4 toggles color on and off)
- **Multi-Page Editing**: Pages stored in REU or disk temp files. Return on a full page pushes the last line onto the next page. Delete at the start of a line joins it with the line above, including the last line of the previous page, and pulls the next page's first line up so only the last page stays short.
- **Directory Browser**: Multi-drive support (8-15) with file type display
- **Search & Replace**: Find text with wrap-around and replace all
- **Copy/Paste**: Visual mark mode. Copy keeps two lines. A marked keyword turns yellow. DEL while lines are marked removes those lines.
- **Undo/Redo**: One-level undo and redo
- **Goto Line**: Jump to any line number
- **Display wrap**: A line longer than the screen continues on the next row. Return starts a new editor line
- **Help**: F8 opens two pages. Right shows page 2, Left shows page 1, Stop or Return leaves
- **40-column mode**: 37x23 editing area with line numbers
- **80-column mode**: 77x23 editing area with line numbers

## Prerequisites

Install the [LLVM-MOS SDK](https://github.com/llvm-mos/llvm-mos-sdk):

```bash
# macOS (Apple Silicon)
curl -sL https://github.com/llvm-mos/llvm-mos-sdk/releases/latest/download/llvm-mos-macos.tar.xz -o /tmp/llvm-mos.tar.xz
mkdir -p ~/llvm-mos && tar -xf /tmp/llvm-mos.tar.xz -C ~/llvm-mos --strip-components=1
```

## Building

```bash
mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=~/llvm-mos ..
make
```

Output: `build/whisper64.prg`, `build/w64conv.prg`, `build/w64renum.prg`, and `build/w64label.prg`

A 512KB REU image (`whisper64.reu`) and a blank D64 disk image (`whisper64.d64`) are created automatically during the build.

## Running

### VICE Emulator

```bash
./run_vice.sh
```

This launches VICE (x64sc) with REU enabled and a D64 disk image attached to drive 8.

### Real Hardware

Transfer `whisper64.prg` to a disk or SD card and load:

```
LOAD "WHISPER64",8,1
RUN
```

For REU support, ensure the REU is connected before starting.

### W64CONV, W64RENUM, and W64LABEL

Three programs sit next to the editor. On the machine:

```
LOAD "W64CONV",8,1
RUN

LOAD "W64RENUM",8,1
RUN

LOAD "W64LABEL",8,1
RUN
```

`W64CONV` menu `1` reads a numbered SEQ and writes a PRG. It asks `STRIP REM? Y/N`. `Y` removes a real `REM` and the rest of that line. A `REM` inside quotes stays, and so does `REMEMBER`. A line that is only a REM keeps its line number. It then asks `RENUMBER? Y/N`. No keeps the numbers in the SEQ. Yes asks `STEP:`, and Return on an empty step uses 10. A number from 1 to 63999 renumbers and rewrites `GOTO`, `GOSUB`, `THEN`, `ELSE`, `GO TO`, and `GO SUB`. Menu `2` reads a PRG and writes a SEQ. A long line is cut back to the last whole statement, and the status line counts those cuts as `TRUNC`. Bytes after the BASIC program are not copied (`ML 1`).

`W64RENUM` menu `1` renumbers a SEQ and writes a SEQ. Menu `2` renumbers a PRG and writes a PRG. Menu `2` copies the bytes that follow the BASIC program. Both ask `STEP:`. An empty step is 10.

`W64LABEL` reads a labeled SEQ and writes a PRG. It asks `STRIP REM? Y/N` with the same rules. `start: REM menu` keeps the label. It asks `STEP:`. An empty step is 10.

A long job paints the stage and the line on one row. A SEQ read paints `READING LINE: n`, or `STRIPPING LINE: n` when REM is stripped. A PRG read paints `PRG LINE: n` with the BASIC line number. The next walks paint `PASS 1 LINE: n`, `PASS 2 LINE: n`, then `RENUMBERING LINE: n` or `NUMBERING LABELED LINES: n`, and a PRG write paints `TOKENIZING LINE: n`. `n` starts over when the next walk starts, except `PRG LINE:`. Keeping the SEQ numbers paints only `TOKENIZING LINE: n`. All three tools treat an LF as a line break. A CR followed by an LF is an empty line, the same split the editor uses.

Each one clears the screen, leaves the title and the REU line, and then asks for a read drive and a write drive, 8–15. An empty entry is 8. The editor's F2 save stays a SEQ. N or Stop at the another-file prompt leaves the tool. `?` opens help, and Stop leaves that page. RUN/STOP on the main menu exits.

- Row 1 shows `REU: NONE` or `REU: nKB`. The largest value is `16320KB`. A job needs at least `128KB`. Below that, the job paints `REU 128KB REQUIRED` and waits for a key.
- The file is stored in the REU. `TOO BIG` means that REU filled. A tokenized program can still stop at `PRG TOO BIG` when a 16-bit link would wrap.
- Line numbers stop at 63999, and a line stops at 79 characters.
- Running any of the three overwrites the REU. Save from the editor first when other pages of the open file exist only in the REU.

### bas2seq

`tools/bas2seq` runs on the desktop. From `tools/`, `python3 -m bas2seq` reads a PC `.bas` file and writes a SEQ the editor can open. It does not number lines. `W64LABEL` still does that on the C64.

## Key Commands

| Key              | Function                                                                    |
|------------------|-----------------------------------------------------------------------------|
| **F1**           | Load file (directory browser)                                               |
| **F2**           | Save file                                                                   |
| **F3**           | Select drive (8-15)                                                         |
| **F4**           | Toggle BASIC color on/off                                                   |
| **F5**           | Find text                                                                   |
| **F6**           | Find & replace                                                              |
| **F7**           | Find next                                                                   |
| **F8**           | Help screen                                                                 |
| **CTRL+B**       | Previous page                                                               |
| **CTRL+C**       | Copy marked text                                                            |
| **CTRL+D**       | Toggle 40/80 column mode                                                    |
| **CTRL+G**       | Goto a file-wide line. The title-bar number is 1-based. A page is 48 lines. |
| **CTRL+K**       | Toggle mark mode                                                            |
| **CTRL+N**       | Next page                                                                   |
| **CTRL+V**       | Paste text                                                                  |
| **CTRL+W**       | New file (clear buffer)                                                     |
| **CTRL+Y**       | Redo                                                                        |
| **CTRL+Z**       | Undo                                                                        |
| **HOME**         | Go to start of file                                                         |
| **Arrows**       | Move cursor                                                                 |
| **Shift+letter** | Lowercase. Unshifted letters stay uppercase. Shift Lock types lowercase.    |

## 80-Column Mode

Press **CTRL+D** to toggle between 40 and 80 column modes.

80-column mode uses VIC-II hires bitmap mode with a 4x8 pixel font (SCREEN-80 from Compute's Gazette, 1984). The display is rendered in VIC bank 3 ($C000-$FFFF) with bitmap data at $E000 and video matrix at $D800.

Limitations:

- Each pair of adjacent characters shares one foreground color
- Slower screen updates than 40-column mode (typing updates only the current line for speed)

## REU Support

The editor auto-detects REU at startup and shows the available size and maximum page count. With REU, page swapping is instant (DMA transfer) instead of using slow disk temp files.

Page capacity depends on REU size:

- 256KB: ~66 pages
- 512KB: ~136 pages

All pages are saved to and loaded from REU when switching pages. File save (F2) writes all pages to disk. File load (F1) reads the file and distributes content across pages as needed. On a multi-page load or save the drive finishes the open byte before a page moves through the REU. The same save matches with turbo off. In C64U Turbo Registers mode, startup writes speed index 3 when that index is 0, so typing stays at 4 MHz. Load and save clear the index for the open file and write the saved byte back after close. CPU Speed 1 in Manual still round-trips; no software write makes Manual honor $D031. With turbo left off, the file still matches. `W64CONV`, `W64RENUM`, and `W64LABEL` measure this same REU and use it as a byte store.

## BASIC Mode

Press **F4** to turn BASIC color on. Press it again to turn the color off.

- Statements are purple and functions are blue
- A quote, or a finished REM, leaves the rest of that line uncolored
- The editor does not rewrite line numbers. Renumber and labels are W64RENUM and W64LABEL
- In BASIC mode the 80th character is not stored. The status row says `BASIC LINE FULL`

## Directory Browser

Press **F1** to open:

- Shows disk name, file types (PRG, SEQ, DEL, USR, REL), block sizes
- **UP/DOWN** to navigate, **RETURN** to load, **RUN/STOP** to cancel

## License

Free to use and modify.