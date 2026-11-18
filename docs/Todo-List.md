# Editor

Mouse and joystick support was removed so display wrap fits under the C64 RAM limit. Do not put it back unless a later link still leaves slack under `$D000`.

## Bugs & Fixes Needed:

- [ ] Replace function freezes the program at the "REPLACE ALL? (Y/N)" message. Appears to be inconsistent, works sometimes and freezes at other times.
- [x] When lines are being pushed down between pages after hitting the enter/return key a line is being lost/disappearing between those pages. Also the line/lines on the next page after the lost line sometimes looses the first character of the line, an "IF CS=1" becomes " F CS=1" Also deleting lines is not pulling the lines up from the previous pages. This is causing my programs to break and work to be lost without knowing it until its to late.

## Features

- [ ] When in mark mode and text is highlighted and the user presses tge DEL delete key it should remove the text lines that are selected.

---

# W64TOOLS (w64conv, w64renum, w64label) - Features

- [ ] Add more detail to the line status. Right now it just appears to loop through the lines multiple times until the conversion is done. Add information about what it is doing to the lines its counting through, example "Pass 1 Line: 23", "Renumbering Line: 10", "Numbering Labeled Lines: 130"

## Bugs on real C64U hardware with 16MB REU enabled and .d64 disk inserted called W64

- [ ] When asking for which drives to read and write to the screen should have cleared before printing the question instead of printing overtop the previous information/menu items that were on screen. 

<!-- # Both Whisper64 Editor and Utility programs  -->
<!-- Scrapped cartridge feature for the moment. Editor code has grown to big to fit the extra needed exit code.
- [ ] Look into converting from standard .prg program files to a .crt cartridge file will all programs on it.
  - [ ] All programs on one cartridge file with a selection menu of which one the user wants to load into.
  - [ ] If this can be done all programs should be able to return to the main program selection menu. The menu should have the option to exit back to the BASIC prompt so the user can run the programs they have written and converted. -->

---

# Feature - Conversion Tool for BASIC .bas files
- [x] Look into a creation of a utility program to convert BASIC files written on the PC in the .bas file format and convert them to a seq file that can be copied to the c64 and opened with the Whisper64 editor. This program will be run on a pc(windows or linux) or a mac, not the c64. It will need to be smart enough to keep the program within the limitations of the C64 and give errors with good explanations to the user when it cannot. Example .bas file is in "/home/josh/Documents/Programming/C64/dice-roll/src/main.bas" it was written with the VS64 extension for Visual Studio code, it can use labeled lines or normal BASIC numbered lines. The example file has a few lines longer than would be able to be converted to work on the C64, this file should give the user an error message and possible example ways to fix it. If a fix is simple enough possibly give the user the ability to visually see and accept a fix that the program will continue by making that change, example: Shorting a line that is too long by using short forms like ? in place of PRINT. The program is `tools/bas2seq`, started with `python3 -m bas2seq` from `tools/`.
    - Simple TUI that lets the user select the file with a file browser and also select the output destination and name for the converted file.
    - A display window for the user to see the errors, scroll through them and confirm any recommended changes to fix the error.
    - Example of how it may look/work:  
      +-------------------------------+
      | File |  Progam Name Here      |
      +-------------------------------+
      | 10 Print "Hello"              |
      | Error: Reason here            |
      | Replace with: 10 ? "Hello"    |
      | [Y] [N]                       |
      |_______________________________+

## bas2seq Bugs & Fixes needed:
- [x] File browser cannot move out of the folder the program is launched from. If run from the the whisper64-extended/tools folder the browser is stuck in the tools folder, I cannot navigate outside of the folder to select a .bas file from another folder such as my "/home/josh/Documents/Programming/C64/dice-roll/src/main.bas" The list is one folder. `..` is the parent, including when the launch folder is `tools/`. Enter on a `.bas` file loads it.
- [x] Along with being able to type the output directory there needs to be a way to selected the output files path with a file browser the same way as selecting the input file. The save screen uses that list. The folder on screen is copied into the directory field. Enter on a file fills the directory and the name, then the existing Y/N overwrite check runs. The directory and name fields still submit what was typed.
- [x] PRINT is being detected as a label. No BASIC functions/tokens should be getting detected as labels. If the w64label program detects this as a label it will also need to be fixed as well.
    A name that is exactly an uppercase V2 token is not a label in `bas2seq` or `W64LABEL`. `PRINT:` and `SGN:` stay statements, and a second `PRINT:` is not `duplicate label`. `PRINT1:` is still a label. `print:` is still a label. Host checks passed. Not run on the Ultimate.
- [ ] Instead of deleting/removing comments we should be recommending putting them to a new line.
- [ ] Need better separation of the error / issue line and the possible fix for the user to accept. Currently they blend together in the terminal if you are not looking closely.
- [ ] Add a number indicator for how many errors there are to deal with and what one the user is currently on.
- [ ] If there is errors the program cannot fix itself it should display them to the user first so they don't waste time running through a bunch of fixable issues first.
