#include "help.h"
#include "screen.h"

static void help_page(int page) {
    clrscr();
    if (page == 0) {
        cputs_at(0, 0, "WHISPER64 - HELP", COL_YELLOW);
        cputs_at(0, 2, "FUNCTION KEYS:", COL_CYAN);
        cputs_at(2, 3, "F1 - LOAD FILE (DIRECTORY)", COL_WHITE);
        cputs_at(2, 4, "F2 - SAVE FILE", COL_WHITE);
        cputs_at(2, 5, "F3 - SELECT DRIVE (8-15)", COL_WHITE);
        cputs_at(2, 6, "F4 - BASIC COLOR ON/OFF", COL_WHITE);
        cputs_at(2, 7, "F5 - FIND TEXT", COL_WHITE);
        cputs_at(2, 8, "F6 - FIND & REPLACE", COL_WHITE);
        cputs_at(2, 9, "F7 - FIND NEXT", COL_WHITE);
        cputs_at(2, 10, "F8 - THIS HELP SCREEN", COL_WHITE);

        cputs_at(0, 11, "EDITING:", COL_CYAN);
        cputs_at(2, 12, "CTRL+K - TOGGLE MARK MODE", COL_WHITE);
        cputs_at(2, 13, "CTRL+C - COPY MARKED TEXT", COL_WHITE);
        cputs_at(2, 14, "CTRL+V - PASTE TEXT", COL_WHITE);
        cputs_at(2, 15, "CTRL+Z - UNDO LAST CHANGE", COL_WHITE);
        cputs_at(2, 16, "CTRL+Y - REDO LAST CHANGE", COL_WHITE);
        cputs_at(2, 17, "CTRL+G - GOTO LINE NUMBER", COL_WHITE);
        cputs_at(2, 18, "CTRL+W - NEW FILE", COL_WHITE);
        cputs_at(2, 19, "HOME   - GO TO TOP", COL_WHITE);
        cputs_at(2, 20, "ARROWS - MOVE CURSOR", COL_WHITE);

        cputs_at(0, 23, "> NEXT PAGE", COL_CYAN);
        return;
    }

    cputs_at(0, 0, "WHISPER64 - HELP 2", COL_YELLOW);
    
    cputs_at(2, 2, "CTRL+D - 40/80 COLUMNS", COL_WHITE);
    cputs_at(2, 3, "CTRL+N NEXT PAGE  CTRL+B BACK", COL_WHITE);
    cputs_at(2, 4, "BASIC LINE STOPS AT 79 CHARACTERS", COL_WHITE);

    cputs_at(0, 6, "TOOL PROGRAMS:", COL_CYAN);
    cputs_at(0, 7, "W64CONV SEQ/PRG:", COL_YELLOW);
    cputs_at(1, 8, "READS A SEQ BASIC FILE AND WRITES A PRG", COL_WHITE);
    cputs_at(1, 9, "CONVERTS A PRG BACK TO A SEQ BASIC FILE", COL_WHITE);
    cputs_at(0, 10, "W64RENUM RENUMBER:", COL_YELLOW);
    cputs_at(1, 11, "RENUMBERS A SEQ BASIC FILE", COL_WHITE);
    cputs_at(0, 12, "W64LABEL LABELS:", COL_YELLOW);
    cputs_at(1, 13, "CONVERTS A LABELED BASIC FILE TO A PRG", COL_WHITE);

    cputs_at(0, 23, "< PREVIOUS PAGE - STOP EXITS", COL_CYAN);
}

void show_help() {
    int page = 0;

    for (;;) {
        char c;
        help_page(page);
        c = cgetc();
        if (c == KEY_RIGHT)
            page = 1;
        else if (c == KEY_LEFT)
            page = 0;
        else if (c == KEY_STOP || c == KEY_RETURN)
            break;
    }
    update_cursor();
}
