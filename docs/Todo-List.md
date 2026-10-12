## Bugs & Fixes Needed:
- [ ] Check into how lines are displayed. May need some form of line wrapping implemented as it seems some lines are leaving the screen in both 40 column and 80 column display modes.
- [ ] Check that all C64 characters are supported as some characters seem to be showing up garbbled in 40 column mode but then display fine in 80 column mode.

## Features
- [x] Highlighting of BASIC when in 80 Column mode
- [x] Ability to load and export/save BASIC to .prg file instead of just .seq
  W64CONV, built and distributed next to the editor, reads a SEQ from the drive the user picks and writes a tokenized BASIC V2 PRG. It also reads a PRG and writes a SEQ the editor can load. The editor itself saves SEQ only.
- [x] Support a joystick for mouse cursor movement when in mouse mode
- [ ] Ability to select which port the joystick in plugged into.
