# BAS to SEQ conversion tool

A desktop program reads a PC `.bas` file, the kind VS64 writes, and writes a SEQ file the Whisper64 editor can open. Labels stay in the file. `W64LABEL` on the C64 still assigns the line numbers. The editor, `W64CONV`, `W64RENUM`, and `W64LABEL` stay as they are.

The source may use labels, numbered lines, or both. A numbered line is optional spaces, then digits, then a space or the end of the line. `10PRINT` is a code line. A number is from 0 through 63999.

## Where it lives

The program is `tools/bas2seq/`. CMake does not build it.

| Path | Role |
|---|---|
| `tools/bas2seq/engine.py` | Load, rename, propose, apply, warn, write |
| `tools/bas2seq/app.py` | Textual screens |
| `tools/bas2seq/__main__.py` | `python3 -m bas2seq`, run from `tools/` |
| `tools/bas2seq/requirements.txt` | Lists `textual` |
| `tools/test_bas2seq.py` | Host checks for the engine |
| `tools/fixtures/bas2seq_sample.bas` | One sample with the shapes below |

The engine owns every text change. The screen picks files, shows a proposal, collects a typed name, and returns the key the user pressed. Coloring a line for display does not change the bytes that will be written.

## Pipeline

1. Read the file as UTF-8 and split it on LF, CR LF, or CR. A final line with no line ending is kept. Blank lines stay. Source line numbers are 1-based and count blank lines.
2. Build the label table. Ask for any name that still needs typing.
3. Apply the rename map to a working copy.
4. Build one proposal, or one blocking reason, for each source line that still breaks a limit.
5. The user accepts or rejects each proposal. A blocking reason is unresolved.
6. The user accepts or rejects the rename map.
7. Warnings are computed on the text that would be written.
8. The SEQ is written only when nothing is unresolved and the map was accepted. An empty map counts as accepted.

A file that needs no typed names skips that prompt. A file with no renames skips the map. A file whose lines already fit skips the line walk. The warning list still appears before the write, and it may be empty.

The line walk shows the source line as loaded. The replacement uses the new label names. A line whose only change is a rename is left out of the walk and listed on the map.

## Line replacements

A line of 79 characters or under, with none of the rewrites below, is copied unchanged. The 79 count the text only. The CR written to the SEQ is extra.

On the working copy, outside quotes, `REM`, and `DATA`, the phrases `GO TO` and `GO SUB` become `GOTO` and `GOSUB`. One or more spaces between the two words match. The word boundary is the one in `src/conv/renumber.c`: a letter on either side means it is not the phrase. If that join is the only change and every resulting piece fits in 79, the proposal is the join. The reason says `GO TO joined` or `GO SUB joined`.

A longer line then walks this ladder. The first step that makes every piece fit is the proposal. Later steps run only when an earlier step still leaves a piece over 79. The reason names each step that changed the text, using the phrases below.

Spaces, keywords, and colons follow the tokenizer. A quote toggles string mode. `REM` outside quotes ends the scan for the rest of that line. `DATA` outside quotes keeps its spaces until a colon outside quotes. `PRINT#` is a separate keyword and stays written as `PRINT#`.

1. **Spaces collapsed.** Repeated spaces outside quotes, `REM`, and `DATA` become one space. If the line now fits, that is the proposal.
2. **Comment removed.** When a `REM` has at least one non-space character before it, and the text before that `REM` fits in 79 with the spaces immediately in front of `REM` removed, the comment is dropped. A colon that then ends the line is dropped with it, so the line does not end on an empty statement. A line that is only a comment is left for step 5.
3. **PRINT shortened.** A whole word `PRINT` outside quotes and `REM` becomes `?`. The match is the uppercase word, it is not a letter on either side, and the next character is not `#`.
4. **Split at colons.** Each colon outside quotes, `REM`, and `DATA` starts a new editor line. A label stays on the first piece. One space after a colon is dropped when the next piece starts. A piece that is empty or only spaces is dropped.
5. **Comment wrapped, or assignment split.** Used on a piece that is still over 79.
   - A `REM` piece is broken onto following `REM` lines. Each line is `REM`, one space, then at most 75 characters of comment. The break is the last space inside that budget. That space is the separator and is not copied. A remaining word longer than 75 characters stops the ladder.
   - A concatenation assignment is broken at a `+` outside quotes. The name is a letter, then letters or digits, then an optional `$` or `%`. The rewritten lines use one space around `=` and `+`. The label, when there is one, stays on the first line with one space after its colon. The first line packs as many operands as fit after `NAME =`. Each later line is `NAME = NAME +` and then as many further operands as fit. One operand that cannot fit on its line stops the ladder.

If the ladder stops, the screen shows the source line and the reason. There is no replacement, and the run is unresolved. The same is true for a long statement that is neither a comment nor a `name = ... + ...` assignment.

`labelFoo: PRINT "A": GOTO labelBar` can become two lines, with the label on the first. `REM Note: short names` is wrapped as a comment, and the colon stays in the comment.

## Labels and jumps

A label is the start of a line: optional spaces, a letter, then letters or digits, then a colon. A legal name is at most 11 characters. Matching keeps the case of the source. `_` and any other character end the name, so `label_foo:` is not a label `W64LABEL` will take. A name that is exactly one entry in `TOKEN_KEYWORDS` is not a label. The compare is case-sensitive, so `PRINT:` and `SGN:` are statements, and `print:` is still a label. `PRINT1:` is a label because the whole name has to match.

The map is built before the line walk. A name of 11 characters or under stays as written. `labelSetup` is 10 characters and stays `labelSetup`.

A longer name is replaced in one of two ways:

- If it begins with the exact lowercase prefix `label` and the rest starts with a letter, that prefix is removed. When the rest is 11 characters or under and matches no kept name and no other candidate, the candidate is the replacement.
- Otherwise, and when the candidate is still too long or collides, the user types the replacement before the line walk. Two long names that produce the same candidate both need a typed name. A legal typed name is a letter, then letters or digits, at most 11, and unique. The prompt stays up until the name is legal.

`labelActivateSel` becomes `ActivateSel`. `labelStartSelRoll` becomes `StartSelRoll`, which is still 12 characters, so it is typed. `labelChooseDrive` becomes `ChooseDrive`. `labelChooseDriveKP` becomes `ChooseDriveKP`, which is still 13 characters, so it is typed.

Two definitions that are the same name before this step are a blocking error. The prompt is only for a name the prefix rule could not finish.

After the line walk the screen shows every rename and each use after `GOTO`, `GOSUB`, `THEN`, or `ELSE`, including a comma list such as `GOTO a, b`. One accept applies the whole map. One reject leaves the run unresolved and writes nothing.

A keyword after `THEN` or `ELSE` stays a statement. A mapped label in that spot is a jump, and the map updates the name. Keywords are the array in `src/conv/token.c`, in that order. `GOTO` is before `GO`, and `PRINT#` is before `PRINT`.

A number after `GOTO`, `GOSUB`, `THEN`, or `ELSE` stays a number. Each number in a comma list counts. The number has to be the number of some numbered line in the file. A missing number is a blocking error on the line that contains the jump.

These also stop the write, and they have no replacement:

- The same label twice after the map is applied.
- A jump to a name that is never defined.
- A colon-name `W64LABEL` will not take, such as `label_foo:`.
- A numbered line whose number is above 63999.
- The same line number on two lines.
- A character outside ASCII 32 through 126, including a tab.

The source file has to be valid UTF-8. A decoded character outside that ASCII range is the same blocking error. Braces and the other symbols inside the range are kept.

When a line has a blocking character, that is the reason shown. The ladder does not propose a rewrite of that line.

## Warnings

Warnings are computed on the text that would be written. They are listed. The SEQ can still be written.

- A keyword that contains a letter, matched outside quotes and `REM`, whose spelling is not the exact uppercase entry. `print` and `Print` are listed. The tool leaves the spelling as written. `+` and the other symbol keywords have no case.
- Two variables with different spellings, the same type, and the same first two characters, matched in case. A one-character name stands alone, so `A` and `AB` are different. `AB` and `ABC` are the same. `AB` and `ab` are different. The type is the plain name, `$`, or `%`. `COLCODE` and `COLOR` are one variable. `COLCODE` and `CO$` are two. A label definition, a keyword, the name that follows `FN`, and a name used as a jump are not variables.

## Highlighting

Every BASIC source line the screen shows is colored. That is the original line, each replacement line, and each line listed on the rename map. Reasons, prompts, and the warning text stay in the screen's normal color.

The scan is the editor's scan in `src/screen.c`. The names and colors are the lists in `src/editor_state.c`: statements are purple, functions are blue. One-character operators are not in that list. A quote toggles string mode, and the quoted characters stay the normal color. `(` ends a word. `TAB(` and `SPC(` keep the parenthesis and are colored. A finished word `REM` is purple, and the rest of that line stays the normal color. `REMEMBER` is not a comment.

On this screen, purple is the terminal's magenta and blue is blue. The normal color is the screen's ordinary text. A `?` outside quotes and outside `REM` is magenta, because the SEQ stores it as the PRINT token. The scan matches uppercase words, so a lowercase keyword stays the normal color and is still listed as a warning.

## Screens

Escape on the file screen leaves the program. Escape on any later screen drops the current conversion and returns to the file screen. Nothing is written in either case.

1. **Files.** A directory tree starts at the current working directory and picks the `.bas` file. A second prompt picks the folder and the output name. The name starts as the source name with `.seq` in place of its extension, in the same directory. The user can change the folder and the name. If that path exists, Y replaces it and N returns to the name prompt.
2. **Typed names.** One prompt per name the prefix rule could not finish. Enter submits. An empty, too-long, or already-used name stays on the prompt with the reason under it.
3. **Lines.** A proposal shows the source line number, the original text, the reason, and the replacement. The original and the replacement are highlighted. The replacement may be several lines. Y keeps it. N marks it unresolved. A blocking line shows the highlighted original and the reason, and Enter moves on. That line is unresolved. The walk continues after an N.
4. **Stop or continue.** When anything is unresolved, the screen lists those source lines and reasons, then returns to the file step. When every line was accepted and the map is non-empty, the map is shown. Y continues. N returns to the file step.
5. **Warnings.** The list scrolls. W writes the file. Escape returns to the file step.
6. **Done.** The screen shows the path, the number of editor lines written, and the number of warnings. A key returns to the file step.

A missing file, a file that cannot be read, and a file that is not UTF-8 are reported at the file step. If the output folder cannot be written, or the write fails, the screen shows the operating-system error and keeps the accepted text so another path can be chosen.

Blocking reasons name the source line and use these phrases, plus a short explanation of what to change in the `.bas` file when there is no replacement:

| Situation | Phrase |
|---|---|
| Duplicate label | `duplicate label` |
| Jump to an unknown name | `unknown label` |
| Jump to an unknown number | `unknown line` |
| Illegal label character | `label character` |
| Line number above 63999 | `line number` |
| The same line number twice | `duplicate line` |
| Byte outside ASCII 32–126 | `character` |
| Comment word too long | `comment word` |
| `+` operand too long | `expression piece` |
| Long statement with no split | `line length` |

## SEQ file

The tool writes one host file. The user copies it onto a disk. Every editor line ends with CR (`0x0D`), including the last. The file contains no LF. The bytes are the ASCII text of the finished lines.

## Tests

`python3 tools/test_bas2seq.py` calls the engine. It does not open the screen, VICE, or the Ultimate. The sample fixture lives in the repo. `dice-roll/src/main.bas` stays a manual run.

The sample fixture includes a 215-character `TP$ = ... + ...` line, a long `REM`, a trailing comment padded with spaces, `GO TO`, `labelStartSelRoll`, and both `labelChooseDrive` and `labelChooseDriveKP`. The sample asks for typed names only for `labelStartSelRoll` and `labelChooseDriveKP`.

The script checks:

- A line of 79 or under is copied with its spaces, its `PRINT`, and its comment. Blank lines stay.
- The ladder stops at the first step that fits. Repeated spaces collapse only on a long line, and they stay inside quotes, `REM`, and `DATA`. A trailing `REM` is dropped when the rest fits, and a line that is only a comment is wrapped instead. `PRINT` becomes `?`, and `PRINT#` stays. A colon inside quotes, `REM`, or `DATA` stays. A label stays on the first piece of a split, and an empty piece between colons is dropped.
- A long comment wraps on word spaces. One word that cannot fit on a `REM` line blocks the write. A concatenation breaks into `name = ...` and `name = name + ...`. One operand that cannot fit blocks the write. A long `PRINT` of a single string blocks the write.
- A name of 11 or under is kept, including `labelSetup`. A longer name loses a leading `label` when the rest is legal and unique. `labelStartSelRoll` and `labelChooseDriveKP` come back as names that still have to be typed. A typed name is rejected when it is empty, too long, or already used.
- The map rewrites `GOTO`, `GOSUB`, `THEN`, `ELSE`, and a comma list. A keyword after `THEN` stays. `GO TO name` becomes `GOTO name` on a short line.
- The blocking cases in the table above name the source line and include the phrase.
- Warnings list `print` and `Print`, and they list `COLCODE` with `COLOR`. `COLCODE` and `CO$` produce no warning. A label and a jump name are not treated as variables.
- The keyword tuple in the engine matches the array in `src/conv/token.c`, in the same order.
- The highlighter marks `PRINT` and `GOTO` as statements, `SGN` and `CHR$` as functions, and a `?` outside quotes as a statement. A quoted `PRINT`, and the text after `REM`, stay the normal color. `TAB(` includes the parenthesis. The name list matches `basic_keywords` and `basic_colors` in `src/editor_state.c`.
- The writer puts a CR at the end of every line, including the last, and it writes no file while any line is unresolved.

## Done when

Running `python3 -m bas2seq` from `tools/`, after `textual` is installed, a person can pick `tools/fixtures/bas2seq_sample.bas`, accept the proposals, type the two remaining label names, accept the map, and get a `.seq` file whose lines are at most 79 characters and whose labels `W64LABEL` can read. `python3 tools/test_bas2seq.py` passes.
