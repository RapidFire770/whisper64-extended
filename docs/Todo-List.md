# Editor

Mouse and joystick support was removed so display wrap fits under the C64 RAM limit. Do not put it back unless a later link still leaves slack under `$D000`.

## Bugs & Fixes Needed:

- [x] The last line was pushed down a row, so a bug fix on that BASIC line never survived conversion. Typing past the screen width split the buffer into a real extra line (`10 ... CONVERS` then `ON":NEXT`, and `20 PRINT"TEST"` moved down). Display wrap keeps one editor line. A file that was already split stays split until Delete is pressed at the start of the continuation.
- [x] Lines longer than the screen were clipped in both 40- and 80-column mode. The same display wrap shows the rest on the next screen row. The gutter number is only on the first row. Load does not insert a break.
- [x] Line splitting still occurs on the last line of the file if a manual return isn't added to create a new blank line. Upon saving a file the line gets split after the BASIC line number so 10 PRINT "HELLO" becomes just 10 with the PRINT "HELLO" being pushed to the next line bellow. If I manually hit return to create a blank line this does not occur an the saved file looks correct and coverts with the W64CONV program correctly. The last line had no CR, and load dropped the EOI byte.
- [x] The top line of the program does not refresh correctly all the time / gets corrupted. If I enter the help screen and return to the main editing window it will display left over text "ELP" beside the numbers so I will see "1:11ELP". It appears to differ how the corruption of the line looks depending on the length of a loaded files filename. The title row was never blanked, so a shorter name, drive number, or page count left the previous characters.
- [ ] Check that all C64 characters are supported as some characters seem to be showing up garbbled in 40 column mode but then display fine in 80 column mode. In 40 column the {} are being shown as a square dot and a L shaped graphic character but then display correctly in the 80 column mode.
- [ ] Do not syntax highlight inside of a REM comment

## Features

- [x] Highlighting of BASIC when in 80 Column mode
- [x] Ability to load and export/save BASIC to .prg file instead of just .seq W64CONV, built and distributed next to the editor, reads a SEQ from the drive the user picks and writes a tokenized BASIC V2 PRG. It also reads a PRG and writes a SEQ the editor can load. The editor itself saves SEQ only.
- [ ] Full BASIC V2 highlighting with different colors instead of all purple.
- [ ] In BASIC mode make sure the line cannot be longer than the C64 supports for BASIC programming. If limit is hit warn the user.
- [ ] Full file renumbering instead of just the page displayed. Allow user to input the renumber increment. If program size is an issue for this feature we can look at moving it into the W64CONV program.

---

# W64CONV - Features

- [ ] Nicer interface and workflow:
  - [ ] Black background, gray border, Geen Text, Centered Title
  - [ ] Help screen that will give details on program use and what the error messages mean.
  - [ ] 1\. User selects which type of conversion they want.
  - [ ] 2\. Ask user for drive to read from. (Display available drive numbers)
  - [ ] 3\. Ask user for drive to write to.
  - [ ] 4\. Show a list of files on the read disk and allow user to navigate up and down that list to select the file they want to load.
  - [ ] 5\. Status screen for convervsion. After finished asks the user if they want to convert another file or exit the program. Exiting should clear the screen back to a C64 ready prompt.
- [ ] Support for BASIC files using labels instead of line numbers. In VScode with the VS64 plugin you can write basic with no line numbers but use a label e.g. "labelmenu:" and when compiled it will add line numbers to the file and change all label references in goto, gosub, etc. to the correct line numbers.
