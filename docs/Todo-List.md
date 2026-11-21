# Whisper64 Editor

Mouse and joystick support was removed so display wrap fits under the C64 RAM limit. Do not put it back unless a later link still leaves slack under `$D000`.

## Bugs & Fixes Needed:

- [ ] Replace function freezes the program at the "REPLACE ALL? (Y/N)" message. Appears to be inconsistent, works sometimes and freezes at other times.
- [x] When lines are being pushed down between pages after hitting the enter/return key a line is being lost/disappearing between those pages. Also the line/lines on the next page after the lost line sometimes looses the first character of the line, an "IF CS=1" becomes " F CS=1" Also deleting lines is not pulling the lines up from the previous pages. This is causing my programs to break and work to be lost without knowing it until its to late.
- [x] Saving issue still occurs where a single character gets corrupted / changed with a different character even after the removing the SD2IEC and Epyx Fastload combo cart. In the C64U file browser the file will also sometimes not allow the user to scroll the whole file after some saves/overwrites of the file, the original file you can scroll through the whole file where the broken version will stop at a certain line partway through the file, example - diceroll-src.seq will save after being modified and then when using the w64label program to convert it will error on line 144 saying line long even though that line has not been changed and would convert before the save, then in the view option of the c64u file browser it will let you scroll down to that line but then cannot view anything else in the file even though the whole file appears to load fine in the whisper64 editor. This occurred while having the c64u set with turbo mode on manual at 4mhz to allow fluid line scrolling while using 80 column mode, it occurred on multiple test saves to different files. Turning the turbo mode down to 2mhz and trying again appeared to save okay but more testing is required to see if the turbo mode is the cause or just a coincidence and starts to happen again with the lower turbo setting or when its disabled as the turbo is not supposed to interfere with disk access operations because the CPU is supposed to return to 1mhz for disk access.

  Page-end bytes matched at CPU Speed 1 and at 4 MHz. The open file drops to 1 MHz and the saved turbo byte is written back. The compare is `tools/test_iec_quiet_c64u.py`.

  > The **Commodore 64 Ultimate** (and **Ultimate 64 Elite-II**) features a **Turbo Mode** that overclocks the 6510 CPU from the standard **1 MHz** up to **48 MHz** (original U64) or **64 MHz** (Elite-II).  This mode decouples the CPU from the VIC-II bus timing, allowing for significantly faster processing without the screen corruption typical of original hardware modifications.
  >
  > ### **Configuration and Control**
  >
  > Turbo speed and behavior are managed through three primary control methods, selectable in the firmware menu:
  > - **Manual**: The system runs at the set speed for memory access and downshifts to 1 MHz for I/O space (disk access). This is best for general speed boosts where program-specific control is not needed. 
  > - **U64 Turbo Registers**: Allows software to programmatically control speed via register **$D031**.  Bits 0-3 set the specific speed index (e.g., 0=1MHz, 15=48/64MHz), and Bit 7 controls **Badline Timing** (disabling badlines allows more CPU cycles but breaks timing compatibility for some games). 
  > - **Turbo Enable Bit**: Uses register **$D030** (similar to the **Commodore 128**).  Writing 1 turns turbo on using the menu-selected speed, and 0 turns it off. This is often used by games that support C128 enhancements.
  >
  > ### **Hardware and Compatibility Notes**
  > - **Badline Timing**: On the original **C64**, the CPU is halted during "bad lines" (when the VIC-II accesses RAM).  In Turbo Mode, disabling badlines (Bit 7 of $D031) allows the CPU to run faster, but this can cause timing-sensitive games to malfunction.
  > - **Bus Operation Mode**: When using physical external hardware (like **EasyFlash** or **IDE64**) alongside Turbo Mode, the **Bus Operation Mode** in the menu may need to be changed from "Quiet" to "Dynamic" or "Writes" to ensure proper signal bridging.
  > - **Performance**: While turbo mode significantly speeds up data loading and CPU-intensive tasks, it does not linearly speed up all games. Titles that are heavily dependent on the VIC-II raster timing may show minimal visual improvement or exhibit glitches if not specifically coded to handle the higher clock speeds.

  As of November 19th there is a new firmware update for the Commodore 64 Ultimate bringing it from v1.1.0 to v1.2.1 the change log located in `~/C64 and Amiga/C64U/Firmware/v1.2.1/changelog-1.2.1.txt` indicates there are fixes to disk drive handling. I have update the firmware to this version incase it has any relation to the issues we are seeing with saving files from the whisper64 editor. Software IEC is also now enabled and the SD card directory to the “MY FILES” folder is mounted as device #11, this can be used for read and write testing as well as the mounted W64.d64 disk on device #8

  After testing on the new firmware the bug presented itself again. Bad files after write testing on the W64.d64 disk are DICEROLL-SRC,DICEROLL-SRC2, DICEROLL-TEST. DICEROLL-BACKUP is the good file.

## Features

- [x] When in mark mode and text is highlighted and the user presses the DEL delete key it should remove the text lines that are selected.

---

# W64TOOLS (w64conv, w64renum, w64label) - Features

- [x] Add more detail to the line status. Right now it just appears to loop through the lines multiple times until the conversion is done. Add information about what it is doing to the lines its counting through, example "Pass 1 Line: 23", "Renumbering Line: 10", "Numbering Labeled Lines: 130"

## Bugs on real C64U hardware with 16MB REU enabled and .d64 disk inserted called W64

- [x] When asking for which drives to read and write to the screen should have cleared before printing the question instead of printing overtop the previous information/menu items that were on screen.

<!-- # Both Whisper64 Editor and Utility programs  -->
<!-- Scrapped cartridge feature for the moment. Editor code has grown to big to fit the extra needed exit code.

- [ ] Look into converting from standard .prg program files to a .crt cartridge file will all programs on it.
  - [ ] All programs on one cartridge file with a selection menu of which one the user wants to load into.
  - [ ] If this can be done all programs should be able to return to the main program selection menu. The menu should have the option to exit back to the BASIC prompt so the user can run the programs they have written and converted. -->

---

# bas2seq Features:

- [ ] Package to install on system and be run without having to cd into folder and run without python -m bas2seq. 
- [ ] User to just be able to type `bas2seq filename.bas` at the command line then select the output directory and output seq filename

## bas2seq Bugs & Fixes needed:

- [x] File browser cannot move out of the folder the program is launched from. If run from the the whisper64-extended/tools folder the browser is stuck in the tools folder, I cannot navigate outside of the folder to select a .bas file from another folder such as my "/home/josh/Documents/Programming/C64/dice-roll/src/main.bas" The list is one folder. `..` is the parent, including when the launch folder is `tools/`. Enter on a `.bas` file loads it.
- [x] Along with being able to type the output directory there needs to be a way to selected the output files path with a file browser the same way as selecting the input file. The save screen uses that list. The folder on screen is copied into the directory field. Enter on a file fills the directory and the name, then the existing Y/N overwrite check runs. The directory and name fields still submit what was typed.
- [x] PRINT is being detected as a label. No BASIC functions/tokens should be getting detected as labels. If the w64label program detects this as a label it will also need to be fixed as well. A name that is exactly an uppercase V2 token is not a label in `bas2seq` or `W64LABEL`. `PRINT:` and `SGN:` stay statements, and a second `PRINT:` is not `duplicate label`. `PRINT1:` is still a label. `print:` is still a label. Host checks passed. Not run on the Ultimate.
- [ ] Instead of deleting/removing REM comments we should be recommending putting them to a new line.
- [ ] Need better separation of the error / issue lines and the possible fix for the user to accept. Currently they blend together in the terminal if you are not looking closely.
- [ ] Add a number indicator for how many errors there are to deal with and what one the user is currently on.
- [ ] If there is errors the program cannot fix itself it should display them to the user first so they don't waste time running through a bunch of fixable issues first.