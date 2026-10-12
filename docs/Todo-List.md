# Editor
Mouse and Joystick support is a nice to have but not needed if removal will save enough space to keep more advanced editing features.
## Bugs & Fixes Needed:
- [ ] Files will save on first write but editing and saving them again is not working. The file that should be overwritten with the changes stays the same as its original write. I made a d64 disk on the C64U called W64.d64 with a file called TESTFILE on it to try to convert which failed because it needed a bug fix edit. When I open the file and make the fix and then save it the file remains the same with the bug unfixed.
- [ ] Check into how lines are displayed. May need some form of line wrapping implemented as it seems some lines are leaving the right side of the screen in both 40 column and 80 column display modes.
- [ ] Check that all C64 characters are supported as some characters seem to be showing up garbbled in 40 column mode but then display fine in 80 column mode. In 40 column the {} are being shown as a square dot and a L shaped graphic character but then display correctly in the 80 column mode.
- [ ] Do not syntax highlight inside of a REM comment
- [ ] The top line of the program does not refresh correctly all the time. If I enter the help screen and return to the main editing window it will display left over text "ELP" beside the numbers so I will see "1:11ELP"

## Features
- [x] Highlighting of BASIC when in 80 Column mode
- [x] Ability to load and export/save BASIC to .prg file instead of just .seq
  W64CONV, built and distributed next to the editor, reads a SEQ from the drive the user picks and writes a tokenized BASIC V2 PRG. It also reads a PRG and writes a SEQ the editor can load. The editor itself saves SEQ only.
- [x] Support a joystick for mouse cursor movement when in mouse mode
- [ ] Ability to select which port the joystick in plugged into.
- [ ] Full BASIC V2 highlighting with different colors instead of all purple.
- [ ] Full file renumbering instead of just the page displayed.
- [ ] In BASIC mode make sure the line cannot be longer than the C64 supports for BASIC programming. If limit is hit warn the user.

---

# W64CONV - Features
- [ ] Nicer interface and workflow:
  - [ ] Black background, gray border, Geen Text, Centered Title
  - [ ] Help screen that will give details on program use and what the error messages mean.
  - [ ] 1. User selects which type of conversion they want.
  - [ ] 2. Ask user for drive to read from. (Display available drive numbers)
  - [ ] 3. Ask user for drive to write to.
  - [ ] 4. Show a list of files on the read disk and allow user to navigate up and down that list to select the file they want to load.
  - [ ] 5. Status screen for convervsion. After finished asks the user if they want to convert another file or exit the program. Exiting should clear the screen back to a C64 ready prompt.
- [ ] Support for BASIC files using labels instead of line numbers. In VScode with the VS64 plugin you can write basic with no line numbers but use a label e.g. "labelmenu:" and when compiled it will add line numbers to the file and change all label references in goto, gosub, etc. to the correct line numbers.
