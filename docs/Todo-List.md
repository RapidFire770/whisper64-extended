# Editor

Mouse and joystick support was removed so display wrap fits under the C64 RAM limit. Do not put it back unless a later link still leaves slack under `$D000`.

## Bugs & Fixes Needed:

- [x] The last line was pushed down a row, so a bug fix on that BASIC line never survived conversion. Typing past the screen width split the buffer into a real extra line (`10 ... CONVERS` then `ON":NEXT`, and `20 PRINT"TEST"` moved down). Display wrap keeps one editor line. A file that was already split stays split until Delete is pressed at the start of the continuation.
- [x] Lines longer than the screen were clipped in both 40- and 80-column mode. The same display wrap shows the rest on the next screen row. The gutter number is only on the first row. Load does not insert a break.
- [x] Line splitting still occurs on the last line of the file if a manual return isn't added to create a new blank line. Upon saving a file the line gets split after the BASIC line number so 10 PRINT "HELLO" becomes just 10 with the PRINT "HELLO" being pushed to the next line bellow. If I manually hit return to create a blank line this does not occur an the saved file looks correct and coverts with the W64CONV program correctly. The last line had no CR, and load dropped the EOI byte.
- [x] The top line of the program does not refresh correctly all the time / gets corrupted. If I enter the help screen and return to the main editing window it will display left over text "ELP" beside the numbers so I will see "1:11ELP". It appears to differ how the corruption of the line looks depending on the length of a loaded files filename. The title row was never blanked, so a shorter name, drive number, or page count left the previous characters.
- [x] Check that all C64 characters are supported correctly as some characters seem to be showing up garbbled in 40 column mode but then display fine in 80 column mode. In 40 column the {} are being shown as a square dot and a L shaped graphic character or the [] show up as a large + and | but then display correctly in the 80 column mode. Appears to be some kind of incorrect mapping in one mode but correct in the other. 40-column draw maps `@` `[` `]` `|` and `a`–`z` onto the lowercase character set. `[]` are brackets. Braces, backslash, caret, underscore, backtick, and tilde are not in the character ROM, so those bytes stay the distinct ROM graphics and are not aliased onto the brackets. The buffer bytes are unchanged. 80-column mode still uses the ASCII 4x8 font.

## Features

- [x] Highlighting of BASIC when in 80 Column mode
- [x] Ability to load and export/save BASIC to .prg file instead of just .seq W64CONV, built and distributed next to the editor, reads a SEQ from the drive the user picks and writes a tokenized BASIC V2 PRG. It also reads a PRG and writes a SEQ the editor can load. The editor itself saves SEQ only. The program is now `w64tools.prg`. On the machine, `LOAD "W64TOOLS",8,1`.
- [x] Do not syntax highlight inside of a REM comment. A quote toggles string mode for the rest of the scan. A finished word REM colors that word and leaves the rest of the editor line alone. REMEMBER is not a comment. Checked in VICE: `10 PRINT "REM" : REM GOTO` colors PRINT and the real REM, and leaves the quoted REM and the GOTO white.
- [x] Full BASIC V2 keyword highlighting with different colors instead of all purple. Statements are purple and functions are blue. One-character operators are not colored. `(` is not part of a word except `TAB(` and `SPC(`. The list is separate from the tokenizer in `src/conv/token.c`. Checked in VICE in both column modes: PRINT purple, SGN blue.
- [x] Brainstorm a way to expand Goto Line function to work across pages. Current pages are 47 lines long. CTRL+G takes the title bar's file-wide row. A page is 48 lines, gutter `00`–`47`. Line 49 is gutter `00` on page 2. A number past the file shows `LINE n NOT FOUND` and stays put. Checked in VICE.
- [x] Keyboard shortcuts to move ahead to next page or back to the previous page. CTRL+N is next page and CTRL+B is the previous page. Both land on row 0. Up from row 0 and Down from the last row still change pages too. Checked in VICE.
- [x] In BASIC mode make sure the line cannot be longer than what the C64 supports for BASIC programming. If limit is hit warn the user. The 80th character is not stored. The status row says `BASIC LINE FULL`. The buffer stays 79 characters plus a NUL. That warning is not followed by a redraw, because a wrapped line's redraw clears the status row. Checked in VICE.
- [x] Second help page. The user can navigate between page 1 and 2 with the arrow keys. Will tell the user the basic information about going and using the W64TOOLS program and any overflowing new keyboard shortcuts. Right shows page 2, Left shows page 1, Stop or Return leaves help and the title row is redrawn. The page 2 strings stay short so the link remains under `$CC00`. Checked in VICE.
- [x] Full file renumbering instead of just the page displayed. Allow user to input the renumber increment. The program is already close to its size limits so we can look at moving this feature into the W64CONV/W64TOOLS program. Mode 3 in W64TOOLS asks for the step, and an empty answer is 10. The editor no longer renumbers the page. Host tests passed. The Ultimate disk round trip was not run.

---

# W64TOOLS - Features
- [x] Rename to W64TOOLS. The built program is `w64tools.prg`.
- [x] Nicer interface and workflow. The screens are linked. They were not shown on a machine.
  - [x] Black background, gray border, Geen Text, Centered Title. Border gray 12, background black, text green 5, selected directory row yellow 7. The title is centered.
  - [x] Help screen that will give details on program use and what the error messages mean. `?` opens it. Stop leaves that page.
  - [x] 1\. User selects which type of conversion they want. Menu keys are 1 through 4.
  - [x] 2\. Ask user for drive to read from. (Display available drive numbers) An empty entry is 8. Anything outside 8–15 says `USE 8-15`.
  - [x] 3\. Ask user for drive to write to. Same range as the read drive.
  - [x] 4\. Show a list of files on the read disk and allow user to navigate up and down that list to select the file they want to load. `NO FILES` waits for a key and returns to the menu.
  - [x] 5\. Status screen for convervsion. After finished asks the user if they want to convert another file or exit the program. Exiting should clear the screen back to a C64 ready prompt. N or Stop returns through the SDK exit-return path. That return was not run on a machine.
- [x] Support for BASIC files using labels instead of line numbers. In VScode with the VS64 plugin you can write basic with no line numbers but use a label e.g. "labelmenu:" and when compiled it will add line numbers to the file and change all label references in goto, gosub, etc. to the correct line numbers. Mode 4 numbers the labels at step 10 and writes a PRG. Host tests passed. The Ultimate disk round trip was not run.
